// Probe: read Ryzen Tctl / CCD temps through PawnIO (needs admin). Logs once per second.
// usage: probe_temp.exe <AMDFamily17.bin> <logfile> <seconds>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define PIO_IOCTL_LOAD 0xA1B22084u
#define PIO_IOCTL_EXEC 0xA1B22104u

static HANDLE g_pio;

static BOOL pio_exec1(const char *fn, ULONG64 arg, ULONG64 *out) {
    BYTE in[40] = {0};
    DWORD ret = 0;
    strncpy((char *)in, fn, 31);
    memcpy(in + 32, &arg, 8);
    return DeviceIoControl(g_pio, PIO_IOCTL_EXEC, in, sizeof in, out, 8, &ret, NULL) && ret == 8;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage\n"); return 2; }
    FILE *log = fopen(argv[2], "w");
    if (!log) return 3;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(log, "cannot open module\n"); fclose(log); return 4; }
    static BYTE blob[65536];
    size_t n = fread(blob, 1, sizeof blob, f);
    fclose(f);

    g_pio = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_pio == INVALID_HANDLE_VALUE) { fprintf(log, "open PawnIO failed %lu\n", GetLastError()); fclose(log); return 5; }
    DWORD ret;
    if (!DeviceIoControl(g_pio, PIO_IOCTL_LOAD, blob, (DWORD)n, NULL, 0, &ret, NULL)) {
        fprintf(log, "load module failed %lu\n", GetLastError()); fclose(log); return 6;
    }
    HANDLE pci = CreateMutexW(NULL, FALSE, L"Global\\Access_PCI");
    int secs = atoi(argv[3]);
    ULONG64 unit = 0, e0 = 0, e1 = 0;
    pio_exec1("ioctl_read_msr", 0xC0010299, &unit);
    double eunit = 1.0 / (double)(1ULL << ((unit >> 8) & 0x1F));
    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf);
    pio_exec1("ioctl_read_msr", 0xC001029B, &e0); QueryPerformanceCounter(&q0);
    Sleep(1000);
    for (int i = 0; i < secs; i++) {
        pio_exec1("ioctl_read_msr", 0xC001029B, &e1); QueryPerformanceCounter(&q1);
        DWORD de = (DWORD)e1 - (DWORD)e0;
        double pkgW = de * eunit / ((double)(q1.QuadPart - q0.QuadPart) / qf.QuadPart);
        e0 = e1; q0 = q1;
        ULONG64 tctl = 0, ccd1 = 0, ccd2 = 0;
        DWORD w = pci ? WaitForSingleObject(pci, 50) : WAIT_OBJECT_0;
        BOOL ok = pio_exec1("ioctl_read_smn", 0x00059800, &tctl);
        pio_exec1("ioctl_read_smn", 0x00059954, &ccd1);
        pio_exec1("ioctl_read_smn", 0x00059958, &ccd2);
        if (pci && (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED)) ReleaseMutex(pci);
        double t = (double)(((DWORD)tctl) >> 21) * 0.125;
        if (tctl & 0x80000) t -= 49.0;
        double c1 = ((double)(ccd1 & 0xFFF) * 125.0 - 305000.0) / 1000.0;
        double c2 = ((double)(ccd2 & 0xFFF) * 125.0 - 305000.0) / 1000.0;
        SYSTEMTIME st; GetLocalTime(&st);
        unsigned long long ms = 0; FILETIME ft; GetSystemTimeAsFileTime(&ft);
        ms = ((((unsigned long long)ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000ULL - 11644473600000ULL;
        fprintf(log, "%llu ok=%d raw=%08lx Tctl=%.3f CCD1=%.3f CCD2=%.3f PKG=%.2f\n", ms, ok, (DWORD)tctl, t, c1, c2, pkgW);
        fflush(log);
        Sleep(1000);
    }
    fclose(log);
    return 0;
}
