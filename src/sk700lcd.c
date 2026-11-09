// sk700lcd.c - Sudokoo SK700V の液晶に CPU 温度を送るだけの最小常駐プログラム
//
// MasterCraft (Electron, 常駐 500MB 超) の代替。CRT なし・Win32 API のみ。
//   - CPU 温度 / 消費電力 / クロック: PawnIO ドライバ + モジュール (exe に埋め込み)
//       AMD  : AMDFamily17.bin (Zen 系, family 17h/19h/1Ah)
//       Intel: IntelMSR.bin    (Nehalem 以降)
//   - CPU 使用率: GetSystemTimes
//   - 液晶への送信: HID (VID 381C / PID 0003) に 1 秒ごとに 2 フレーム書き込む
//
// 使い方:
//   sk700lcd.exe install    サービスとして登録して開始 (Program Files にコピー)
//   sk700lcd.exe uninstall  サービスを停止して削除
//   sk700lcd.exe run        コンソールで実行 (動作確認用, Ctrl+C で終了)
//   sk700lcd.exe test N F   N 秒間実行してログをファイル F に書く (動作確認用)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <sddl.h>
#include <intrin.h>

// ---------------------------------------------------------------------------
// CRT なしで必要な最小限 (単体テスト時は CRT を使うので外す)
// ---------------------------------------------------------------------------
#ifndef SK700_TEST
int _fltused = 1;

#pragma function(memset)
void *__cdecl memset(void *dst, int c, size_t n) {
    __stosb((unsigned char *)dst, (unsigned char)c, n);
    return dst;
}
#pragma function(memcpy)
void *__cdecl memcpy(void *dst, const void *src, size_t n) {
    __movsb((unsigned char *)dst, (const unsigned char *)src, n);
    return dst;
}
#endif

static int iceil(double x) {
    int i = (int)x;
    return (x > (double)i) ? i + 1 : i;
}
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------
// 定数
// ---------------------------------------------------------------------------
#define SVC_NAME     L"SK700LCD"
#define SVC_DISPLAY  L"SK700V LCD (CPU temperature)"
#define SVC_DESC     L"Sudokoo SK700V cooler LCD: sends CPU temperature once per second (MasterCraft replacement)."
#define INSTALL_DIR  L"SK700LCD"
#define INSTALL_EXE  L"sk700lcd.exe"
#define IDR_PAWN_AMD17 101
#define IDR_PAWN_INTEL 102

// HID デバイス
static const GUID GUID_HID = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
#define DEV_MATCH L"vid_381c&pid_0003"
#define REPORT_ID 0x10

// PawnIO
#define PIO_IOCTL_LOAD 0xA1B22084u
#define PIO_IOCTL_EXEC 0xA1B22104u

// AMD Zen
#define AMD_SMN_THM_TCON_CUR_TMP 0x00059800u
#define AMD_TEMP_OFFSET_FLAG     0x00080000u
#define AMD_MSR_PWR_UNIT         0xC0010299u
#define AMD_MSR_PKG_ENERGY_STAT  0xC001029Bu
#define AMD_MSR_HW_PSTATE_STATUS 0xC0010293u
#define AMD_MSR_PSTATE_0         0xC0010064u

// Intel
#define INTEL_MSR_PLATFORM_INFO        0x000000CEu
#define INTEL_MSR_PERF_STATUS          0x00000198u
#define INTEL_MSR_THERM_STATUS         0x0000019Cu
#define INTEL_MSR_TEMPERATURE_TARGET   0x000001A2u
#define INTEL_MSR_PACKAGE_THERM_STATUS 0x000001B1u
#define INTEL_MSR_RAPL_POWER_UNIT      0x00000606u
#define INTEL_MSR_PKG_ENERGY_STATUS    0x00000611u

enum { CPU_UNKNOWN = 0, CPU_AMD, CPU_INTEL };

// ---------------------------------------------------------------------------
// 値の変換 (純粋関数: tools/test_decode.c で単体テスト)
// ---------------------------------------------------------------------------

// AMD THM_TCON_CUR_TMP -> Tctl [℃]
static double amd_tctl(DWORD v) {
    double t = (double)(v >> 21) * 0.125;
    if (v & AMD_TEMP_OFFSET_FLAG) t -= 49.0;
    return t;
}

// AMD P-state (PStateDef / HW_PSTATE_STATUS) -> 倍率 (Zen5 は MHz/100)
static double amd_ratio(ULONG64 v, int family) {
    if (family >= 0x1A) return (double)(v & 0xFFF) * 5.0 / 100.0;
    {
        DWORD fid = (DWORD)(v & 0xFF), dfs = (DWORD)((v >> 8) & 0x3F);
        return dfs ? 2.0 * fid / dfs : 0.0;
    }
}

// Intel IA32_TEMPERATURE_TARGET -> TjMax [℃] (読めない/異常値なら 100)
static double intel_tjmax(ULONG64 v) {
    DWORD t = (DWORD)(v >> 16) & 0xFF;
    return (t >= 50 && t <= 150) ? (double)t : 100.0;
}

// Intel IA32_(PACKAGE_)THERM_STATUS -> 温度 [℃]。bit31 (Reading Valid) が立っていなければ -1000
static double intel_temp(ULONG64 st, double tjmax) {
    if (!(st & 0x80000000u)) return -1000.0;
    return tjmax - (double)((st >> 16) & 0x7F);
}

// Intel IA32_PERF_STATUS -> 倍率
static double intel_ratio(ULONG64 v, int family, int model) {
    if (family == 6) {
        switch (model) {
        case 0x1A: case 0x1E: case 0x1F: case 0x25: case 0x2C: case 0x2E: case 0x2F:  // Nehalem / Westmere
            return (double)(v & 0xFF);
        case 0x0F: case 0x16: case 0x17: case 0x1D:  // Core 2
            return (double)((v >> 8) & 0x1F) + 0.5 * (double)((v >> 14) & 1);
        }
    }
    return (double)((v >> 8) & 0xFF);  // Sandy Bridge 以降
}

// double -> float32 のビット列。MasterCraft と同じく仮数部を丸めずに切り捨てる
// (例: 114.8 は 0x42E59999。四捨五入なら 0x42E5999A)
static DWORD float_bits_trunc(double x) {
    ULONG64 d;
    DWORD sign;
    int e;
    memcpy(&d, &x, 8);
    sign = (DWORD)(d >> 63) << 31;
    e = (int)((d >> 52) & 0x7FF);
    if (e == 0) return sign;
    e = e - 1023 + 127;
    if (e <= 0) return sign;
    if (e >= 255) return sign | 0x7F800000u;
    return sign | ((DWORD)e << 23) | (DWORD)((d & 0xFFFFFFFFFFFFFull) >> 29);
}

// RAPL 電力単位レジスタ -> 1 カウントあたりのジュール
static double energy_unit(ULONG64 v, int vendor, int family, int model) {
    DWORD esu = (DWORD)(v >> 8) & 0x1F;
    if (vendor == CPU_INTEL && family == 6 &&
        (model == 0x37 || model == 0x4A || model == 0x4D || model == 0x5A || model == 0x5D || model == 0x4C))
        return 1.0e-6 * (double)(1u << esu);  // Silvermont / Airmont はマイクロジュール単位
    return 1.0 / (double)(1u << esu);
}

// 液晶用データ (フレーム B の中身 13 バイト)。MasterCraft と同じく各値は切り上げ
//   fahrenheit: 単位フラグ (b[5]) を立て、温度は ceil(℃) × 1.8 + 32 を送る
static void make_sensor_data(BYTE b[13], double temp, double power, double usage, double mhz, BOOL fahrenheit) {
    int t = iclamp(iceil(temp), 0, 150);
    int pw = iclamp(iceil(power), 0, 255);
    int pw2 = pw * 9529 / 10000;  // floor(PW × 0.9529)。記録した全 24 組に一致 (係数 0.9524〜0.9534 なら同じ)
    int us = iclamp(iceil(usage), 0, 100);
    int mz = iclamp(iceil(mhz), 0, 65535);
    DWORD bits = float_bits_trunc(fahrenheit ? 32.0 + 1.8 * (double)t : (double)t);
    b[0] = 0x01; b[1] = 0x02; b[2] = 0x00;
    b[3] = (BYTE)pw; b[4] = (BYTE)pw2; b[5] = fahrenheit ? 0x01 : 0x00;
    b[6] = (BYTE)(bits >> 24); b[7] = (BYTE)(bits >> 16); b[8] = (BYTE)(bits >> 8); b[9] = (BYTE)bits;
    b[10] = (BYTE)us;
    b[11] = (BYTE)(mz >> 8); b[12] = (BYTE)mz;
}

// 画面オフ時に MasterCraft が毎秒送る固定データ (フレーム A は送らない)
static const BYTE SCREEN_OFF_DATA[13] = {0x01, 0x00, 0x00, 0x64, 0x32, 0x00, 0x42, 0x70, 0x00, 0x00, 0x28, 0x0E, 0x10};

// HID 出力レポートを組み立てる: [10] 68 01 09 <len> <data> <sum(68..data)> 16, len まで 0 埋め
static BOOL make_report(BYTE *buf, int len, const BYTE *data, int n) {
    BYTE cs = 0;
    int i;
    if (7 + n > len) return FALSE;
    memset(buf, 0, (size_t)len);
    buf[0] = REPORT_ID;
    buf[1] = 0x68; buf[2] = 0x01; buf[3] = 0x09; buf[4] = (BYTE)n;
    memcpy(buf + 5, data, (size_t)n);
    for (i = 1; i < 5 + n; i++) cs = (BYTE)(cs + buf[i]);
    buf[5 + n] = cs;
    buf[6 + n] = 0x16;
    return TRUE;
}

// ---------------------------------------------------------------------------
// 出力 (コンソール or ログファイル)
// ---------------------------------------------------------------------------
static HANDLE g_out = INVALID_HANDLE_VALUE;
static BOOL g_outIsConsole;

static void out_w(const WCHAR *s) {
    DWORD n = (DWORD)lstrlenW(s), wr;
    char buf[2048];
    int len;
    if (g_out == INVALID_HANDLE_VALUE || g_out == NULL) return;
    if (g_outIsConsole) {
        WriteConsoleW(g_out, s, n, &wr, NULL);
        return;
    }
    len = WideCharToMultiByte(CP_UTF8, 0, s, (int)n, buf, sizeof buf, NULL, NULL);
    if (len > 0) WriteFile(g_out, buf, (DWORD)len, &wr, NULL);
}

// 小さな文字列ビルダ
typedef struct { WCHAR b[256]; int n; } SB;
static void sb_s(SB *s, const WCHAR *t) { while (*t && s->n < 255) s->b[s->n++] = *t++; s->b[s->n] = 0; }
static void sb_i(SB *s, int v) {
    WCHAR t[12]; int i = 0; unsigned u;
    if (v < 0) { sb_s(s, L"-"); u = (unsigned)(-v); } else u = (unsigned)v;
    do { t[i++] = (WCHAR)(L'0' + u % 10); u /= 10; } while (u && i < 11);
    while (i && s->n < 255) s->b[s->n++] = t[--i];
    s->b[s->n] = 0;
}
static void sb_x(SB *s, unsigned v) {  // 16 進
    WCHAR t[9]; int i = 0;
    do { t[i++] = L"0123456789ABCDEF"[v & 0xF]; v >>= 4; } while (v && i < 8);
    while (i && s->n < 255) s->b[s->n++] = t[--i];
    s->b[s->n] = 0;
}
static void sb_f1(SB *s, double v) {  // 小数1桁
    int x = (int)(v * 10.0 + (v >= 0 ? 0.5 : -0.5));
    if (x < 0) { sb_s(s, L"-"); x = -x; }
    sb_i(s, x / 10); sb_s(s, L"."); sb_i(s, x % 10);
}

// ---------------------------------------------------------------------------
// CPU 識別
// ---------------------------------------------------------------------------
static int g_vendor, g_family, g_model;
static BOOL g_intelDts, g_intelPkgTemp;

static void cpu_identify(void) {
    int r[4], maxLeaf, base;
    __cpuid(r, 0);
    maxLeaf = r[0];
    if (r[1] == 0x68747541 && r[3] == 0x69746E65 && r[2] == 0x444D4163) g_vendor = CPU_AMD;         // AuthenticAMD
    else if (r[1] == 0x756E6547 && r[3] == 0x49656E69 && r[2] == 0x6C65746E) g_vendor = CPU_INTEL;  // GenuineIntel
    else g_vendor = CPU_UNKNOWN;
    __cpuid(r, 1);
    base = (r[0] >> 8) & 0xF;
    g_family = base;
    if (base == 0xF) g_family += (r[0] >> 20) & 0xFF;
    g_model = (r[0] >> 4) & 0xF;
    if (base == 0x6 || base == 0xF) g_model |= ((r[0] >> 16) & 0xF) << 4;
    if (g_vendor == CPU_INTEL && maxLeaf >= 6) {
        __cpuid(r, 6);
        g_intelDts = (r[0] & 0x01) != 0;      // コア温度センサー
        g_intelPkgTemp = (r[0] & 0x40) != 0;  // パッケージ温度センサー
    }
}

static BOOL cpu_supported(void) {
    return (g_vendor == CPU_AMD && g_family >= 0x17) || (g_vendor == CPU_INTEL && g_family >= 6);
}

// ---------------------------------------------------------------------------
// PawnIO (CPU センサー)
// ---------------------------------------------------------------------------
static HANDLE g_pio = INVALID_HANDLE_VALUE;
static HANDLE g_pciMutex;
static double g_busMhz = 100.0;
static double g_tjmax = 100.0;
static DWORD g_msrPkgEnergy;
static double g_energyUnit;
static DWORD g_lastEnergy;
static LONGLONG g_lastEnergyQpc;
static LONGLONG g_qpf;
static DWORD_PTR g_coreMask[256];
static int g_coreCount;

static BOOL pio_call(const char *fn, ULONG64 arg, ULONG64 *out) {
    BYTE in[40];
    ULONG64 o = 0;
    DWORD ret = 0;
    int i;
    memset(in, 0, sizeof in);
    for (i = 0; fn[i] && i < 31; i++) in[i] = (BYTE)fn[i];
    memcpy(in + 32, &arg, 8);
    if (!DeviceIoControl(g_pio, PIO_IOCTL_EXEC, in, sizeof in, &o, sizeof o, &ret, NULL) || ret < 8) return FALSE;
    *out = o;
    return TRUE;
}

static BOOL rdmsr(DWORD msr, ULONG64 *v) { return pio_call("ioctl_read_msr", msr, v); }

// 物理コアごとに論理 CPU を 1 つ選ぶ (コアごとのレジスタを読むため)
static void enum_cores(void) {
    DWORD len = 0, off;
    BYTE *buf;
    g_coreCount = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (!len) return;
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, len);
    if (!buf) return;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
        for (off = 0; off < len && g_coreCount < 256;) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
            if (p->Relationship == RelationProcessorCore && p->Processor.GroupMask[0].Group == 0) {
                KAFFINITY m = p->Processor.GroupMask[0].Mask;
                g_coreMask[g_coreCount++] = (DWORD_PTR)(m & (~m + 1));  // 最下位ビット
            }
            off += p->Size;
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

// 全コアで MSR を読み、decode() の最大値を返す (読めたコアがなければ FALSE)
static BOOL max_over_cores(DWORD msr, double (*decode)(ULONG64), double *best) {
    HANDLE th = GetCurrentThread();
    DWORD_PTR prev = 0;
    BOOL any = FALSE;
    int i;
    ULONG64 v;
    for (i = 0; i < g_coreCount; i++) {
        DWORD_PTR p = SetThreadAffinityMask(th, g_coreMask[i]);
        if (!prev) prev = p;
        if (rdmsr(msr, &v)) {
            double x = decode(v);
            if (x > -500.0 && (!any || x > *best)) { *best = x; any = TRUE; }
        }
    }
    if (prev) SetThreadAffinityMask(th, prev);
    return any;
}

static double dec_amd_clock(ULONG64 v) {
    return (g_family >= 0x1A) ? (double)(v & 0xFFF) * 5.0 : amd_ratio(v, g_family) * g_busMhz;
}
static double dec_intel_clock(ULONG64 v) { return intel_ratio(v, g_family, g_model) * g_busMhz; }
static double dec_intel_core_temp(ULONG64 v) { return intel_temp(v, g_tjmax); }

// TSC 実測値 / 定格倍率 でベースクロック (≒100MHz) を求める
static void calibrate_bus(void) {
    LARGE_INTEGER q0, q1;
    unsigned __int64 t0, t1;
    ULONG64 v = 0;
    double tscMhz, mult, bus;
    if (g_vendor == CPU_AMD) {
        if (!rdmsr(AMD_MSR_PSTATE_0, &v)) return;
        mult = amd_ratio(v, g_family);
    } else {
        if (!rdmsr(INTEL_MSR_PLATFORM_INFO, &v)) return;
        mult = (double)((v >> 8) & 0xFF);  // 最大非ターボ倍率
    }
    if (mult <= 0) return;
    QueryPerformanceCounter(&q0); t0 = __rdtsc();
    Sleep(100);
    QueryPerformanceCounter(&q1); t1 = __rdtsc();
    tscMhz = (double)(t1 - t0) / ((double)(q1.QuadPart - q0.QuadPart) / (double)g_qpf) / 1e6;
    bus = tscMhz / mult;
    if (bus > 90.0 && bus < 110.0) g_busMhz = bus;
}

static void pio_close(void) {
    if (g_pio != INVALID_HANDLE_VALUE) CloseHandle(g_pio);
    g_pio = INVALID_HANDLE_VALUE;
}

static BOOL pio_init(void) {
    HRSRC rs;
    HGLOBAL rg;
    DWORD sz, ret, msrUnit;
    ULONG64 v;
    LARGE_INTEGER q;
    if (!cpu_supported()) return FALSE;
    rs = FindResourceW(NULL, MAKEINTRESOURCEW(g_vendor == CPU_AMD ? IDR_PAWN_AMD17 : IDR_PAWN_INTEL), (LPCWSTR)RT_RCDATA);
    if (!rs || !(rg = LoadResource(NULL, rs))) return FALSE;
    sz = SizeofResource(NULL, rs);
    g_pio = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_pio == INVALID_HANDLE_VALUE) return FALSE;
    if (!DeviceIoControl(g_pio, PIO_IOCTL_LOAD, LockResource(rg), sz, NULL, 0, &ret, NULL)) {
        pio_close();
        return FALSE;
    }
    // AMD の SMN アクセスを HWiNFO / LibreHardwareMonitor などと排他する共通ミューテックス
    if (g_vendor == CPU_AMD && !g_pciMutex) {
        PSECURITY_DESCRIPTOR sd = NULL;
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE};
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL))
            sa.lpSecurityDescriptor = sd;
        g_pciMutex = CreateMutexW(&sa, FALSE, L"Global\\Access_PCI");
        if (sd) LocalFree(sd);
    }
    if (g_vendor == CPU_INTEL) {
        g_tjmax = rdmsr(INTEL_MSR_TEMPERATURE_TARGET, &v) ? intel_tjmax(v) : 100.0;
        msrUnit = INTEL_MSR_RAPL_POWER_UNIT;
        g_msrPkgEnergy = INTEL_MSR_PKG_ENERGY_STATUS;
    } else {
        msrUnit = AMD_MSR_PWR_UNIT;
        g_msrPkgEnergy = AMD_MSR_PKG_ENERGY_STAT;
    }
    // 消費電力 (RAPL) の単位と初期値
    g_energyUnit = rdmsr(msrUnit, &v) ? energy_unit(v, g_vendor, g_family, g_model) : 0;
    if (rdmsr(g_msrPkgEnergy, &v)) {
        g_lastEnergy = (DWORD)v;
        QueryPerformanceCounter(&q);
        g_lastEnergyQpc = q.QuadPart;
    }
    enum_cores();
    calibrate_bus();
    return TRUE;
}

static BOOL read_temp(double *t) {
    ULONG64 v;
    if (g_vendor == CPU_AMD) {
        BOOL ok;
        if (g_pciMutex) {
            DWORD w = WaitForSingleObject(g_pciMutex, 100);
            if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) return FALSE;
        }
        ok = pio_call("ioctl_read_smn", AMD_SMN_THM_TCON_CUR_TMP, &v);
        if (g_pciMutex) ReleaseMutex(g_pciMutex);
        if (!ok) return FALSE;
        *t = amd_tctl((DWORD)v);  // HWiNFO の "CPU (Tctl/Tdie)"
        return TRUE;
    }
    // Intel: HWiNFO の "CPU Package"。非対応ならコア温度の最大値
    if (g_intelPkgTemp && rdmsr(INTEL_MSR_PACKAGE_THERM_STATUS, &v)) {
        double x = intel_temp(v, g_tjmax);
        if (x > -500.0) { *t = x; return TRUE; }
    }
    return g_intelDts && max_over_cores(INTEL_MSR_THERM_STATUS, dec_intel_core_temp, t);
}

static BOOL read_power(double *w) {
    ULONG64 v;
    LARGE_INTEGER q;
    double dt;
    DWORD de;
    if (g_energyUnit <= 0 || !rdmsr(g_msrPkgEnergy, &v)) return FALSE;
    QueryPerformanceCounter(&q);
    de = (DWORD)v - g_lastEnergy;  // 32bit カウンタの折り返しも吸収
    dt = (double)(q.QuadPart - g_lastEnergyQpc) / (double)g_qpf;
    g_lastEnergy = (DWORD)v;
    g_lastEnergyQpc = q.QuadPart;
    if (dt < 0.2 || dt > 5.0) return FALSE;
    *w = (double)de * g_energyUnit / dt;
    return TRUE;
}

static BOOL read_clock(double *mhz) {  // 全コア中の最大クロック
    double best = 0;
    if (!max_over_cores(g_vendor == CPU_AMD ? AMD_MSR_HW_PSTATE_STATUS : INTEL_MSR_PERF_STATUS,
                        g_vendor == CPU_AMD ? dec_amd_clock : dec_intel_clock, &best) || best <= 0)
        return FALSE;
    *mhz = best;
    return TRUE;
}

// CPU 使用率 (%)
static ULONGLONG g_lastIdle, g_lastTotal;
static double read_usage(void) {
    FILETIME fi, fk, fu;
    ULONGLONG idle, total, di, dt;
    if (!GetSystemTimes(&fi, &fk, &fu)) return 0;
    idle = ((ULONGLONG)fi.dwHighDateTime << 32) | fi.dwLowDateTime;
    total = (((ULONGLONG)fk.dwHighDateTime << 32) | fk.dwLowDateTime) + (((ULONGLONG)fu.dwHighDateTime << 32) | fu.dwLowDateTime);
    di = idle - g_lastIdle;
    dt = total - g_lastTotal;
    g_lastIdle = idle;
    g_lastTotal = total;
    if (!dt || di > dt) return 0;
    return (double)(dt - di) * 100.0 / (double)dt;
}

// ---------------------------------------------------------------------------
// HID (液晶)
// ---------------------------------------------------------------------------
static HANDLE g_hid = INVALID_HANDLE_VALUE;
static HANDLE g_hidEvent;
static USHORT g_outLen;

static BOOL contains_ci(const WCHAR *s, const WCHAR *pat) {
    for (; *s; s++) {
        const WCHAR *a = s, *b = pat;
        while (*a && *b) {
            WCHAR ca = *a, cb = *b;
            if (ca >= L'A' && ca <= L'Z') ca += 32;
            if (cb >= L'A' && cb <= L'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return TRUE;
    }
    return FALSE;
}

static void hid_close(void) {
    if (g_hid != INVALID_HANDLE_VALUE) CloseHandle(g_hid);
    g_hid = INVALID_HANDLE_VALUE;
}

static BOOL hid_open(void) {
    ULONG len = 0;
    WCHAR *list, *p;
    if (CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)&GUID_HID, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || len < 2)
        return FALSE;
    list = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!list) return FALSE;
    if (CM_Get_Device_Interface_ListW((LPGUID)&GUID_HID, NULL, list, len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS) {
        for (p = list; *p && g_hid == INVALID_HANDLE_VALUE; p += lstrlenW(p) + 1) {
            HANDLE h;
            PHIDP_PREPARSED_DATA pp;
            HIDP_CAPS caps;
            if (!contains_ci(p, DEV_MATCH)) continue;
            h = CreateFileW(p, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            if (HidD_GetPreparsedData(h, &pp)) {
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength >= 21 && caps.OutputReportByteLength <= 256) {
                    g_outLen = caps.OutputReportByteLength;
                    g_hid = h;
                }
                HidD_FreePreparsedData(pp);
            }
            if (g_hid != h) CloseHandle(h);
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    return g_hid != INVALID_HANDLE_VALUE;
}

static BOOL hid_write(const BYTE *buf, DWORD len) {
    OVERLAPPED ov;
    DWORD wr = 0;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = g_hidEvent;
    ResetEvent(g_hidEvent);
    if (!WriteFile(g_hid, buf, len, NULL, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return FALSE;
        if (WaitForSingleObject(g_hidEvent, 1000) != WAIT_OBJECT_0) {
            CancelIo(g_hid);
            GetOverlappedResult(g_hid, &ov, &wr, TRUE);
            return FALSE;
        }
    }
    return GetOverlappedResult(g_hid, &ov, &wr, FALSE) && wr == len;
}

static BOOL send_frame(const BYTE *data, int n) {
    BYTE buf[256];
    if (g_hid == INVALID_HANDLE_VALUE || !make_report(buf, g_outLen, data, n)) return FALSE;
    return hid_write(buf, g_outLen);
}

// ---------------------------------------------------------------------------
// 設定 (HKLM\SOFTWARE\SK700LCD)
// SK700V 本体は設定を保存しない。MasterCraft も毎秒のフレームに含めて送っているだけ。
// ---------------------------------------------------------------------------
#define CFG_KEY L"SOFTWARE\\SK700LCD"
#define SVC_CONTROL_RELOAD 128  // ユーザー定義のサービス制御コード: 設定の再読み込み

typedef struct { DWORD alert, screen, fahrenheit; } CONFIG;
static CONFIG g_cfg = {1, 1, 0};  // 既定: 温度警告オン・画面オン・℃ (MasterCraft の初期値と同じ)
static volatile LONG g_reload;

static const struct { const WCHAR *arg, *value; } CFG_ITEMS[3] = {
    {L"alert", L"TemperatureAlert"}, {L"screen", L"ScreenOn"}, {L"unit", L"Fahrenheit"}};

static DWORD reg_get(HKEY k, const WCHAR *name, DWORD def) {
    DWORD v, sz = sizeof v, type;
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS && type == REG_DWORD) return v ? 1 : 0;
    return def;
}

static void config_load(void) {
    HKEY k;
    CONFIG c = {1, 1, 0};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CFG_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        c.alert = reg_get(k, CFG_ITEMS[0].value, 1);
        c.screen = reg_get(k, CFG_ITEMS[1].value, 1);
        c.fahrenheit = reg_get(k, CFG_ITEMS[2].value, 0);
        RegCloseKey(k);
    }
    g_cfg = c;
}

// ---------------------------------------------------------------------------
// メインループ
// ---------------------------------------------------------------------------
static HANDLE g_stop;
static volatile LONG g_reopen;
static double g_temp = -1, g_power, g_mhz;
static BOOL g_verbose;

static void tick(int n) {
    static int hidRetry, pioRetry;
    double usage = read_usage(), v;
    BOOL lcdOk = FALSE;

    if (InterlockedExchange(&g_reload, 0)) config_load();
    if (g_pio == INVALID_HANDLE_VALUE) {
        if (pioRetry-- <= 0) { pioRetry = 10; pio_init(); }
    }
    if (g_pio != INVALID_HANDLE_VALUE) {
        if (read_temp(&v)) g_temp = v;
        if (read_power(&v)) g_power = v;
        if (read_clock(&v)) g_mhz = v;
    }

    if (InterlockedExchange(&g_reopen, 0)) { hid_close(); hidRetry = 0; }
    if (g_hid == INVALID_HANDLE_VALUE && hidRetry-- <= 0) {
        hidRetry = 3;
        hid_open();
    }

    if (g_hid != INVALID_HANDLE_VALUE && (!g_cfg.screen || g_temp >= 0)) {
        if (g_cfg.screen) {
            BYTE a[2];
            BYTE b[13];
            a[0] = 0x03;
            a[1] = (BYTE)g_cfg.alert;  // フレーム A = 温度警告 (03 01 オン / 03 00 オフ)
            make_sensor_data(b, g_temp, g_power, usage, g_mhz, g_cfg.fahrenheit);
            lcdOk = send_frame(a, 2);
            if (lcdOk) {
                WaitForSingleObject(g_stop, 16);
                lcdOk = send_frame(b, 13);
            }
        } else {
            lcdOk = send_frame(SCREEN_OFF_DATA, 13);
        }
        if (!lcdOk) { hid_close(); hidRetry = 1; }
    }

    if (g_verbose) {
        SB s;
        s.n = 0; s.b[0] = 0;
        sb_s(&s, L"["); sb_i(&s, n); sb_s(&s, L"] CPU ");
        if (g_temp >= 0) { sb_f1(&s, g_temp); sb_s(&s, L"C"); } else sb_s(&s, L"--");
        sb_s(&s, L"  "); sb_f1(&s, g_power); sb_s(&s, L"W  ");
        sb_f1(&s, usage); sb_s(&s, L"%  ");
        sb_i(&s, iceil(g_mhz)); sb_s(&s, L"MHz  LCD:");
        sb_s(&s, lcdOk ? L"OK" : (g_hid == INVALID_HANDLE_VALUE ? L"未接続" : (g_temp < 0 ? L"温度待ち" : L"送信失敗")));
        if (g_pio == INVALID_HANDLE_VALUE) sb_s(&s, L"  (PawnIO 未接続)");
        if (!g_cfg.screen) sb_s(&s, L"  [画面オフ]");
        sb_s(&s, L"\r\n");
        out_w(s.b);
    }
}

static void run_loop(int seconds) {
    LARGE_INTEGER q;
    LONGLONG next;
    int n = 0;
    read_usage();
    QueryPerformanceCounter(&q);
    next = q.QuadPart;
    for (;;) {
        LONGLONG waitMs;
        // 初回も 1 秒待つ (消費電力・使用率の差分に基準値が必要なため)
        next += g_qpf;
        QueryPerformanceCounter(&q);
        waitMs = (next - q.QuadPart) * 1000 / g_qpf;
        if (waitMs < 0) { next = q.QuadPart; waitMs = 0; }
        if (waitMs > 1000) waitMs = 1000;
        if (WaitForSingleObject(g_stop, (DWORD)waitMs) != WAIT_TIMEOUT) break;
        tick(++n);
        if (seconds > 0 && n >= seconds) break;
    }
}

static void trim_memory(void) { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

// ---------------------------------------------------------------------------
// サービス
// ---------------------------------------------------------------------------
static SERVICE_STATUS_HANDLE g_ssh;
static SERVICE_STATUS g_ss;

static void svc_set(DWORD state, DWORD exitCode) {
    g_ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ss.dwCurrentState = state;
    g_ss.dwControlsAccepted = (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_POWEREVENT) : 0;
    g_ss.dwWin32ExitCode = exitCode;
    g_ss.dwWaitHint = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) ? 3000 : 0;
    SetServiceStatus(g_ssh, &g_ss);
}

static DWORD WINAPI svc_ctrl(DWORD ctrl, DWORD type, LPVOID data, LPVOID ctx) {
    (void)data; (void)ctx;
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        svc_set(SERVICE_STOP_PENDING, NO_ERROR);
        SetEvent(g_stop);
        return NO_ERROR;
    case SERVICE_CONTROL_POWEREVENT:
        if (type == PBT_APMRESUMEAUTOMATIC || type == PBT_APMRESUMESUSPEND) InterlockedExchange(&g_reopen, 1);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    case SVC_CONTROL_RELOAD:
        InterlockedExchange(&g_reload, 1);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI svc_main(DWORD argc, LPWSTR *argv) {
    (void)argc; (void)argv;
    g_ssh = RegisterServiceCtrlHandlerExW(SVC_NAME, svc_ctrl, NULL);
    if (!g_ssh) return;
    svc_set(SERVICE_START_PENDING, NO_ERROR);
    svc_set(SERVICE_RUNNING, NO_ERROR);
    config_load();
    pio_init();
    trim_memory();
    run_loop(0);
    hid_close();
    pio_close();
    svc_set(SERVICE_STOPPED, NO_ERROR);
}

// ---------------------------------------------------------------------------
// install / uninstall
// ---------------------------------------------------------------------------
static BOOL install_path(WCHAR *dir, WCHAR *exe) {  // MAX_PATH 以上のバッファ
    DWORD n = GetEnvironmentVariableW(L"ProgramW6432", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) n = GetEnvironmentVariableW(L"ProgramFiles", dir, MAX_PATH);
    if (!n || n + 32 >= MAX_PATH) return FALSE;
    lstrcatW(dir, L"\\" INSTALL_DIR);
    lstrcpyW(exe, dir);
    lstrcatW(exe, L"\\" INSTALL_EXE);
    return TRUE;
}

static void print_err(const WCHAR *what) {
    SB s;
    s.n = 0;
    sb_s(&s, what); sb_s(&s, L" (エラー "); sb_i(&s, (int)GetLastError()); sb_s(&s, L")\r\n");
    out_w(s.b);
}

static void stop_service(SC_HANDLE svc) {
    SERVICE_STATUS st;
    int i;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &st)) {
        for (i = 0; i < 50 && QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_STOPPED; i++) Sleep(100);
    }
}

static int do_install(void) {
    WCHAR self[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 2];
    SC_HANDLE scm, svc;
    SERVICE_DESCRIPTIONW desc;
    SC_ACTION acts[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}};
    SERVICE_FAILURE_ACTIONSW fa;
    SERVICE_REQUIRED_PRIVILEGES_INFOW priv;
    SERVICE_SID_INFO sid;

    if (!GetModuleFileNameW(NULL, self, MAX_PATH) || !install_path(dir, exe)) { print_err(L"パス取得に失敗"); return 1; }
    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { print_err(L"サービスマネージャを開けません (管理者として実行してください)"); return 1; }

    // 既存サービスがあれば停止してから上書き
    svc = OpenServiceW(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (svc) stop_service(svc);

    CreateDirectoryW(dir, NULL);
    if (lstrcmpiW(self, exe) != 0 && !CopyFileW(self, exe, FALSE)) {
        print_err(L"Program Files へのコピーに失敗");
        if (svc) CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return 1;
    }
    lstrcpyW(cmd, L"\"");
    lstrcatW(cmd, exe);
    lstrcatW(cmd, L"\"");

    if (svc) {
        ChangeServiceConfigW(svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmd, NULL, NULL, NULL, NULL, NULL, SVC_DISPLAY);
    } else {
        svc = CreateServiceW(scm, SVC_NAME, SVC_DISPLAY, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                             SERVICE_ERROR_NORMAL, cmd, NULL, NULL, NULL, NULL, NULL);
        if (!svc) { print_err(L"サービス登録に失敗"); CloseServiceHandle(scm); return 1; }
    }

    desc.lpDescription = (LPWSTR)SVC_DESC;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
    memset(&fa, 0, sizeof fa);
    fa.dwResetPeriod = 86400;
    fa.cActions = 3;
    fa.lpsaActions = acts;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
    // 権限を最小化 (LocalSystem でも不要な特権は持たせない)
    priv.pmszRequiredPrivileges = (LPWSTR)L"SeChangeNotifyPrivilege\0SeCreateGlobalPrivilege\0";
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &priv);
    sid.dwServiceSidType = SERVICE_SID_TYPE_UNRESTRICTED;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_SERVICE_SID_INFO, &sid);

    if (!StartServiceW(svc, 0, NULL) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        print_err(L"サービス開始に失敗");
    } else {
        out_w(L"インストールして開始しました: ");
        out_w(exe);
        out_w(L"\r\n");
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int do_uninstall(void) {
    WCHAR dir[MAX_PATH], exe[MAX_PATH];
    SC_HANDLE scm, svc;
    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { print_err(L"サービスマネージャを開けません (管理者として実行してください)"); return 1; }
    svc = OpenServiceW(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (svc) {
        stop_service(svc);
        if (DeleteService(svc)) out_w(L"サービスを削除しました\r\n");
        else print_err(L"サービス削除に失敗");
        CloseServiceHandle(svc);
    } else {
        out_w(L"サービスは登録されていません\r\n");
    }
    CloseServiceHandle(scm);
    if (install_path(dir, exe)) {
        if (!DeleteFileW(exe) && GetLastError() != ERROR_FILE_NOT_FOUND) MoveFileExW(exe, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryW(dir);
    }
    // set コマンドで保存した設定も消す
    if (RegDeleteKeyExW(HKEY_LOCAL_MACHINE, CFG_KEY, KEY_WOW64_64KEY, 0) == ERROR_SUCCESS) out_w(L"設定 (HKLM\\" CFG_KEY L") を削除しました\r\n");
    out_w(L"PawnIO は他のソフトも使うので残しています。不要ならアプリ一覧からアンインストールしてください\r\n");
    return 0;
}

static BOOL service_running(void) {
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT), svc;
    SERVICE_STATUS st;
    BOOL r = FALSE;
    if (!scm) return FALSE;
    svc = OpenServiceW(scm, SVC_NAME, SERVICE_QUERY_STATUS);
    if (svc) {
        r = QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_STOPPED;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return r;
}

// ---------------------------------------------------------------------------
// set / config
// ---------------------------------------------------------------------------
static void print_config(void) {
    SB s;
    config_load();
    s.n = 0;
    sb_s(&s, L"温度警告 (alert)  : "); sb_s(&s, g_cfg.alert ? L"on  (90℃超で赤い温度表示が点滅)" : L"off"); sb_s(&s, L"\r\n");
    sb_s(&s, L"画面 (screen)     : "); sb_s(&s, g_cfg.screen ? L"on" : L"off"); sb_s(&s, L"\r\n");
    sb_s(&s, L"単位 (unit)       : "); sb_s(&s, g_cfg.fahrenheit ? L"f (℉)" : L"c (℃)"); sb_s(&s, L"\r\n");
    out_w(s.b);
}

// sk700lcd set <alert|screen|unit> <on|off|c|f>
static int do_set(const WCHAR *item, const WCHAR *val) {
    int i, idx = -1;
    DWORD v;
    HKEY k;
    SC_HANDLE scm, svc;
    SERVICE_STATUS st;
    for (i = 0; i < 3; i++) if (!lstrcmpiW(item, CFG_ITEMS[i].arg)) idx = i;
    if (idx == 2) {
        if (!lstrcmpiW(val, L"f")) v = 1;
        else if (!lstrcmpiW(val, L"c")) v = 0;
        else idx = -1;
    } else if (idx >= 0) {
        if (!lstrcmpiW(val, L"on")) v = 1;
        else if (!lstrcmpiW(val, L"off")) v = 0;
        else idx = -1;
    }
    if (idx < 0) {
        out_w(L"使い方: sk700lcd set alert on|off / set screen on|off / set unit c|f\r\n");
        return 2;
    }
    {
        LSTATUS st2 = RegCreateKeyExW(HKEY_LOCAL_MACHINE, CFG_KEY, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &k, NULL);
        if (st2 == ERROR_SUCCESS) {
            st2 = RegSetValueExW(k, CFG_ITEMS[idx].value, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
            RegCloseKey(k);
        }
        if (st2 != ERROR_SUCCESS) {
            SetLastError((DWORD)st2);  // Reg* はエラーを戻り値で返す
            print_err(L"設定を保存できません (管理者として実行してください)");
            return 1;
        }
    }
    // 動作中のサービスに再読み込みさせる
    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    svc = scm ? OpenServiceW(scm, SVC_NAME, SERVICE_USER_DEFINED_CONTROL) : NULL;
    if (svc && ControlService(svc, SVC_CONTROL_RELOAD, &st)) out_w(L"保存してサービスに反映しました\r\n");
    else out_w(L"保存しました (サービスは動作していないので、次の起動時に反映されます)\r\n");
    if (svc) CloseServiceHandle(svc);
    if (scm) CloseServiceHandle(scm);
    print_config();
    return 0;
}

// ---------------------------------------------------------------------------
// エントリポイント
// ---------------------------------------------------------------------------
static BOOL WINAPI console_ctrl(DWORD t) {
    (void)t;
    SetEvent(g_stop);
    return TRUE;
}

// コマンドラインの n 番目 (0=exe) の引数を取り出す (簡易: 空白区切り / ダブルクォート対応)
static BOOL get_arg(int idx, WCHAR *dst, int cap) {
    const WCHAR *p = GetCommandLineW();
    int i;
    for (i = 0;; i++) {
        int n = 0;
        BOOL q = FALSE;
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) return FALSE;
        while (*p && (q || (*p != L' ' && *p != L'\t'))) {
            if (*p == L'"') q = !q;
            else if (i == idx && n < cap - 1) dst[n++] = *p;
            p++;
        }
        if (i == idx) { dst[n] = 0; return TRUE; }
    }
}

static int to_int(const WCHAR *s) {
    int v = 0;
    while (*s >= L'0' && *s <= L'9') v = v * 10 + (*s++ - L'0');
    return v;
}

static void print_cpu_info(void) {
    SB s;
    s.n = 0;
    sb_s(&s, g_vendor == CPU_AMD ? L"AMD" : (g_vendor == CPU_INTEL ? L"Intel" : L"不明な CPU"));
    sb_s(&s, L" family=0x"); sb_x(&s, (unsigned)g_family);
    sb_s(&s, L" model=0x"); sb_x(&s, (unsigned)g_model);
    sb_s(&s, L" cores="); sb_i(&s, g_coreCount);
    sb_s(&s, L" bus="); sb_f1(&s, g_busMhz); sb_s(&s, L"MHz");
    if (g_vendor == CPU_INTEL) {
        sb_s(&s, L" TjMax="); sb_f1(&s, g_tjmax);
        sb_s(&s, g_intelPkgTemp ? L" 温度=パッケージ" : (g_intelDts ? L" 温度=コア最大" : L" 温度センサーなし"));
    }
    sb_s(&s, g_energyUnit > 0 ? L" 電力=RAPL" : L" 電力=なし");
    sb_s(&s, L"\r\n");
    out_w(s.b);
}

static int app_main(void) {
    WCHAR cmd[32], a2[16], a3[MAX_PATH];
    LARGE_INTEGER f;
    DWORD mode;

    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hidEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    cpu_identify();

    if (!get_arg(1, cmd, 32)) {
        // 引数なし: サービスとして起動された場合
        SERVICE_TABLE_ENTRYW tbl[2] = {{(LPWSTR)SVC_NAME, svc_main}, {NULL, NULL}};
        if (StartServiceCtrlDispatcherW(tbl)) return 0;
        lstrcpyW(cmd, L"help");
    }

    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    g_outIsConsole = GetConsoleMode(g_out, &mode);

    if (!lstrcmpiW(cmd, L"install")) return do_install();
    if (!lstrcmpiW(cmd, L"uninstall")) return do_uninstall();
    if (!lstrcmpiW(cmd, L"config")) { print_config(); return 0; }
    if (!lstrcmpiW(cmd, L"set")) {
        WCHAR item[16], val[16];
        if (!get_arg(2, item, 16)) item[0] = 0;
        if (!get_arg(3, val, 16)) val[0] = 0;
        return do_set(item, val);
    }
    if (!lstrcmpiW(cmd, L"run") || !lstrcmpiW(cmd, L"test")) {
        int secs = 0;
        if (!lstrcmpiW(cmd, L"test")) {
            secs = get_arg(2, a2, 16) ? to_int(a2) : 10;
            if (get_arg(3, a3, MAX_PATH)) {
                g_out = CreateFileW(a3, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
                g_outIsConsole = FALSE;
            }
        }
        g_verbose = TRUE;
        if (!cpu_supported()) out_w(L"非対応の CPU です (AMD Ryzen / Intel Core 系のみ対応)\r\n");
        if (service_running()) out_w(L"注意: SK700LCD サービスが動作中です (二重送信になります)\r\n");
        SetConsoleCtrlHandler(console_ctrl, TRUE);
        config_load();
        if (!pio_init()) print_err(L"PawnIO を開けません (管理者権限と PawnIO のインストールが必要)");
        print_cpu_info();
        run_loop(secs);
        hid_close();
        pio_close();
        if (!g_outIsConsole && g_out != INVALID_HANDLE_VALUE) CloseHandle(g_out);
        return 0;
    }

    out_w(L"SK700V LCD - CPU 温度を液晶に送る常駐プログラム\r\n"
          L"  sk700lcd install    サービスとして登録・開始 (管理者)\r\n"
          L"  sk700lcd uninstall  サービスを停止・削除 (管理者)\r\n"
          L"  sk700lcd run        コンソールで実行 (Ctrl+C で終了)\r\n"
          L"  sk700lcd config     液晶の設定を表示\r\n"
          L"  sk700lcd set alert on|off   温度警告 (90℃超で赤い温度表示が点滅)\r\n"
          L"  sk700lcd set screen on|off  画面表示\r\n"
          L"  sk700lcd set unit c|f       温度の単位\r\n");
    return 0;
}

#ifndef SK700_TEST
void __stdcall entry(void) { ExitProcess((UINT)app_main()); }
#endif
