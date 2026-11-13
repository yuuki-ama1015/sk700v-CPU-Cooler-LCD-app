// Unit tests for the pure decode / frame functions in src/sk700lcd.c.
// The Intel path cannot run on an AMD machine (the IntelMSR PawnIO module refuses to load),
// so its register decoding is checked here against known register layouts.
// Build & run: tools\run_tests.bat
#define SK700_TEST
#include "../src/sk700lcd.c"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_fail, g_total;

static void check_d(const char *name, double got, double want) {
    g_total++;
    if (fabs(got - want) > 1e-9) { g_fail++; printf("FAIL %s: got %.6f want %.6f\n", name, got, want); }
}
static void check_i(const char *name, int got, int want) {
    g_total++;
    if (got != want) { g_fail++; printf("FAIL %s: got %d want %d\n", name, got, want); }
}
static void check_hex(const char *name, const BYTE *got, const char *wantHex, int n) {
    char buf[600];
    int i;
    g_total++;
    for (i = 0; i < n; i++) sprintf(buf + i * 2, "%02x", got[i]);
    if (strcmp(buf, wantHex) != 0) { g_fail++; printf("FAIL %s:\n  got  %s\n  want %s\n", name, buf, wantHex); }
}

int main(void) {
    BYTE d[13], rep[64];
    BYTE a[2] = {0x03, 0x01};

    // --- AMD Tctl (raw values captured on a Ryzen 7 5800X) ---
    check_d("amd_tctl offset flag", amd_tctl(0x6a3b0000u), 57.125);
    check_d("amd_tctl no flag", amd_tctl(0x38000000u), 56.0);
    // --- AMD P-state ratio ---
    check_d("amd_ratio zen3 P0 38x", amd_ratio(0x00000898u /*dfs=8 fid=0x98*/, 0x19), 38.0);
    check_d("amd_ratio zen3 48.4x", amd_ratio(0x00000AF2u /*dfs=10 fid=0xF2*/, 0x19), 48.4);
    check_d("amd_ratio zen5", amd_ratio(0x000003C0u /*fid=960 -> 4800MHz*/, 0x1A), 48.0);

    // --- Intel TjMax (IA32_TEMPERATURE_TARGET[23:16]) ---
    check_d("intel_tjmax 100", intel_tjmax(0x00640000u), 100.0);
    check_d("intel_tjmax 95 with offset bits", intel_tjmax(0x055F0A00u), 95.0);
    check_d("intel_tjmax 0 -> default", intel_tjmax(0), 100.0);
    // --- Intel temperature: TjMax - readout[22:16], bit31 = valid ---
    check_d("intel_temp valid", intel_temp(0x88430000u, 100.0), 33.0);
    check_d("intel_temp valid 0 readout", intel_temp(0x80000000u, 95.0), 95.0);
    check_d("intel_temp invalid", intel_temp(0x08430000u, 100.0), -1000.0);
    // --- Intel ratio (IA32_PERF_STATUS) ---
    check_d("intel_ratio alder lake 0x97", intel_ratio(0x00002D00u, 6, 0x97), 45.0);
    check_d("intel_ratio raptor lake 0xB7 57x", intel_ratio(0x1C40003900ull, 6, 0xB7), 57.0);
    check_d("intel_ratio nehalem 0x1E", intel_ratio(0x00000016u, 6, 0x1E), 22.0);
    check_d("intel_ratio core2 half", intel_ratio(0x00004900u, 6, 0x17), 9.5);
    // --- RAPL energy units ---
    check_d("energy_unit intel 14", energy_unit(0x000A0E03u, CPU_INTEL, 6, 0x97), 1.0 / 16384.0);
    check_d("energy_unit silvermont", energy_unit(0x00000505u, CPU_INTEL, 6, 0x37), 32.0e-6);
    check_d("energy_unit amd 16", energy_unit(0x000A1003u, CPU_AMD, 0x19, 0x21), 1.0 / 65536.0);

    // --- second power byte: every (PW, PW2) pair MasterCraft sent ---
    {
        static const int pairs[][2] = {{19, 18}, {20, 19}, {21, 20}, {22, 20}, {27, 25}, {28, 26}, {29, 27}, {30, 28},
                                       {31, 29}, {32, 30}, {33, 31}, {34, 32}, {35, 33}, {36, 34}, {37, 35}, {38, 36},
                                       {39, 37}, {40, 38}, {41, 39}, {43, 40}, {44, 41}, {45, 42}, {68, 64}, {69, 65}};
        int i;
        for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
            make_sensor_data(d, 50, pairs[i][0], 10, 4000);
            g_total++;
            if (d[3] != pairs[i][0] || d[4] != pairs[i][1]) { g_fail++; printf("FAIL pw2 %d -> %d (want %d)\n", d[3], d[4], pairs[i][1]); }
        }
    }

    // --- Frames must match what MasterCraft sent (captured with hid_sniff.js / settings_probe.js) ---
    make_report(rep, 64, a, 2);
    check_hex("frame A (alert on)", rep, "106801090203017816", 9);
    a[1] = 0x00;
    make_report(rep, 64, a, 2);
    check_hex("frame A (alert off)", rep, "106801090203007716", 9);
    a[1] = 0x01;
    // CPU Power 32.37W -> 33, Temperature 54.xx -> 55, Usage 19.57% -> 20, Clock 4841.27 -> 4842
    make_sensor_data(d, 54.25, 32.37, 19.57, 4841.27);
    make_report(rep, 64, d, 13);
    check_hex("frame B idle", rep, "106801090d010200211f00425c00001412ea7016", 20);
    // under load: 67.03W -> 68, 63.13C -> 64, 100% , 4641.63 -> 4642
    make_sensor_data(d, 63.13, 67.03, 100.0, 4641.63);
    check_hex("data B load", d, "01020044400042800000641222", 13);
    // another capture (settings_probe2 baseline): 18.5W -> 19, 49.5C -> 50, 6.3% -> 7, 4842
    make_sensor_data(d, 49.5, 18.5, 6.3, 4841.27);
    make_report(rep, 64, d, 13);
    check_hex("frame B low power", rep, "106801090d010200131200424800000712ea3416", 20);
    // screen off: fixed data, frame A is not sent
    make_report(rep, 64, SCREEN_OFF_DATA, 13);
    check_hex("frame B screen off", rep, "106801090d01000064320042700000280e100e16", 20);
    // padding up to the output report length
    make_report(rep, 64, a, 2);
    g_total++;
    { int i, z = 1; for (i = 9; i < 64; i++) if (rep[i]) z = 0; if (!z) { g_fail++; printf("FAIL padding\n"); } }

    // --- Sleep / resume handling (power_event + gate_tick + read_sensors) ---
    // No device is open here: hid_close / pio_close are no-ops and nothing is written to the registry.
    InitializeCriticalSection(&g_ioLock);
    {
        int i, skipped;
        check_i("gate: runs while awake", gate_tick(), 1);
        g_temp = 55.0;
        power_event(PBT_APMSUSPEND);
        check_i("suspend: flag set", (int)g_suspended, 1);
        check_d("suspend: stale temperature dropped", g_temp, -1.0);
        for (i = 0, skipped = 0; i < 10; i++) skipped += !gate_tick();
        check_i("suspend: no device access while suspended", skipped, 10);
        g_temp = 60.0;
        for (i = 0; i < TEMP_FAIL_MAX + 2; i++) read_sensors();
        check_d("suspend: read_sensors does nothing", g_temp, 60.0);

        power_event(PBT_APMRESUMEAUTOMATIC);
        check_i("resume: flag cleared", (int)g_suspended, 0);
        for (i = 0, skipped = 0; i < RESUME_HOLD_SECONDS; i++) skipped += !gate_tick();
        check_i("resume: waits RESUME_HOLD_SECONDS", skipped, RESUME_HOLD_SECONDS);
        check_i("resume: runs after the wait", gate_tick(), 1);
        power_event(PBT_APMRESUMESUSPEND);  // second notification, sent when the user is present
        check_i("resume: second notification ignored", gate_tick(), 1);
        power_event(PBT_APMRESUMEAUTOMATIC);  // resume without a suspend
        check_i("resume without suspend: ignored", gate_tick(), 1);

        // the resume notification never arrives
        power_event(PBT_APMSUSPEND);
        for (i = 0, skipped = 0; i < SUSPEND_MAX_SECONDS; i++) skipped += !gate_tick();
        check_i("lost resume: waits SUSPEND_MAX_SECONDS", skipped, SUSPEND_MAX_SECONDS);
        check_i("lost resume: then runs", gate_tick(), 1);
        check_i("lost resume: flag cleared", (int)g_suspended, 0);

        // sleeping again during the wait after a resume
        power_event(PBT_APMSUSPEND);
        power_event(PBT_APMRESUMEAUTOMATIC);
        gate_tick();
        power_event(PBT_APMSUSPEND);
        check_i("re-suspend during the wait: no access", gate_tick(), 0);
        power_event(PBT_APMRESUMEAUTOMATIC);
        for (i = 0, skipped = 0; i < RESUME_HOLD_SECONDS; i++) skipped += !gate_tick();
        check_i("re-suspend: full wait again", skipped, RESUME_HOLD_SECONDS);
        check_i("re-suspend: runs after the wait", gate_tick(), 1);

        // the temperature cannot be read (PawnIO is not open in this test)
        g_temp = 60.0;
        g_tempFails = 0;
        for (i = 0; i < TEMP_FAIL_MAX - 1; i++) read_sensors();
        check_d("temp failures: last value kept below the limit", g_temp, 60.0);
        read_sensors();
        check_d("temp failures: value dropped at the limit", g_temp, -1.0);
        check_i("temp failures: reopen PawnIO at once", g_pioRetry, 0);
    }

    printf("%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
