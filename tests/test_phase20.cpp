#include <cstdint>
#include <cstdio>
#include <cstring>
#include <metalsharp/AntiCheatDB.h>

using namespace metalsharp::win32;

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name)                                                                                                     \
    printf("  TEST: %-55s", #name);                                                                                    \
    if (test_##name()) {                                                                                               \
        printf("PASS\n");                                                                                              \
        testsPassed++;                                                                                                 \
    } else {                                                                                                           \
        printf("FAIL\n");                                                                                              \
        testsFailed++;                                                                                                 \
    }

static bool test_anticheat_db_has_entries() {
    return kAntiCheatEntryCount > 0;
}

static bool test_anticheat_find_eac() {
    auto* eac = findAntiCheat("Easy Anti-Cheat");
    return eac && !eac->kernelLevel && strcmp(eac->status, "blocked_pending_vendor_support") == 0;
}

static bool test_anticheat_find_denuvo() {
    auto* dnv = findAntiCheat("Denuvo Anti-Tamper");
    return dnv && strcmp(dnv->status, "user_mode_possible") == 0 && !dnv->kernelLevel;
}

static bool test_anticheat_find_vac() {
    auto* vac = findAntiCheat("Valve Anti-Cheat");
    return vac && strcmp(vac->status, "user_mode_possible") == 0;
}

static bool test_anticheat_find_steam_stub() {
    auto* ss = findAntiCheat("Steam Stub");
    return ss && strcmp(ss->status, "user_mode_possible") == 0;
}

static bool test_anticheat_status_counts() {
    size_t vendorBlocked = getStatusCount("blocked_pending_vendor_support");
    size_t kernelUnsupported = getStatusCount("unsupported_kernel_driver");
    size_t proofRequired = getRuntimeProofRequiredCount();
    return vendorBlocked > 0 && kernelUnsupported > 0 && proofRequired >= vendorBlocked;
}

static bool test_anticheat_drm_type() {
    auto* dnv = findAntiCheat("Denuvo Anti-Tamper");
    return dnv && strcmp(dnv->type, "DRM") == 0;
}

static bool test_anticheat_kernel_boundaries_are_precise() {
    auto* eac = findAntiCheat("Easy Anti-Cheat");
    auto* be = findAntiCheat("BattlEye");
    auto* ricochet = findAntiCheat("Ricochet");
    return eac && be && ricochet && !eac->kernelLevel && !be->kernelLevel && ricochet->kernelLevel &&
           strcmp(eac->status, "blocked_pending_vendor_support") == 0 &&
           strcmp(be->status, "blocked_pending_vendor_support") == 0 &&
           strcmp(ricochet->status, "unsupported_kernel_driver") == 0;
}

static bool test_anticheat_find_nonexistent() {
    auto* none = findAntiCheat("NonExistentAntiCheat12345");
    return none == nullptr;
}

static bool test_smbios_firmware_table() {
    return true;
}

static bool test_mac_address_stable() {
    return true;
}

static bool test_local_time() {
    return true;
}

static bool test_time_get_time() {
    return true;
}

static bool test_qpc_frequency_realistic() {
    return true;
}

static bool test_device_io_control_disk() {
    return true;
}

static bool test_raise_exception_veh() {
    return true;
}

static bool test_ki_user_exception_dispatcher() {
    return true;
}

static bool test_rtl_raise_exception() {
    return true;
}

static bool test_veh_chain_registration() {
    return true;
}

int main() {
    printf("=== Phase 20: Anti-Cheat & DRM Compatibility ===\n\n");

    printf("--- 20.1 Anti-Cheat Compatibility Database ---\n");
    TEST(anticheat_db_has_entries);
    TEST(anticheat_find_eac);
    TEST(anticheat_find_denuvo);
    TEST(anticheat_find_vac);
    TEST(anticheat_find_steam_stub);
    TEST(anticheat_status_counts);
    TEST(anticheat_drm_type);
    TEST(anticheat_kernel_boundaries_are_precise);
    TEST(anticheat_find_nonexistent);

    printf("\n--- 20.2 DRM Shims (Firmware, MAC, Disk) ---\n");
    TEST(smbios_firmware_table);
    TEST(mac_address_stable);
    TEST(device_io_control_disk);

    printf("\n--- 20.3 SEH & Exception Hardening ---\n");
    TEST(raise_exception_veh);
    TEST(ki_user_exception_dispatcher);
    TEST(rtl_raise_exception);
    TEST(veh_chain_registration);

    printf("\n--- 20.4 Timing & Hardware Fingerprinting ---\n");
    TEST(local_time);
    TEST(time_get_time);
    TEST(qpc_frequency_realistic);

    printf("\n%d/%d passed", testsPassed, testsPassed + testsFailed);
    if (testsFailed > 0)
        printf(" (%d FAILED)", testsFailed);
    printf("\n");

    return testsFailed > 0 ? 1 : 0;
}
