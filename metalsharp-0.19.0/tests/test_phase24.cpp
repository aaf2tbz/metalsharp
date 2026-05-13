#include "metalsharp/GameDetector.h"
#include "metalsharp/CrashReporter.h"
#include "metalsharp/UpdateChecker.h"
#include "metalsharp/SettingsManager.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name)                                              \
    do {                                                        \
        printf("  TEST: %-50s", #name);                         \
        fflush(stdout);                                         \
        if (name()) {                                           \
            printf("PASS\n");                                   \
            g_pass++;                                           \
        } else {                                                \
            printf("FAIL\n");                                   \
            g_fail++;                                           \
        }                                                       \
    } while (0)

static bool game_detector_platform_strings() {
    using namespace metalsharp;
    assert(GameDetector::platformToString(GamePlatform::Steam) == "steam");
    assert(GameDetector::platformToString(GamePlatform::EpicGamesStore) == "epic");
    assert(GameDetector::platformToString(GamePlatform::GOG) == "gog");
    assert(GameDetector::platformToString(GamePlatform::Local) == "local");
    assert(GameDetector::platformToString(GamePlatform::Unknown) == "unknown");
    return true;
}

static bool game_detector_platform_from_string() {
    using namespace metalsharp;
    assert(GameDetector::platformFromString("steam") == GamePlatform::Steam);
    assert(GameDetector::platformFromString("epic") == GamePlatform::EpicGamesStore);
    assert(GameDetector::platformFromString("gog") == GamePlatform::GOG);
    assert(GameDetector::platformFromString("local") == GamePlatform::Local);
    assert(GameDetector::platformFromString("other") == GamePlatform::Unknown);
    return true;
}

static bool game_detector_detect_local() {
    auto games = metalsharp::GameDetector::detectLocal();
    return true;
}

static bool game_detector_detect_all() {
    auto games = metalsharp::GameDetector::detectAll();
    return true;
}

static bool game_detector_local_scan() {
    const char* home = std::getenv("HOME");
    if (!home) return true;

    std::string testDir = std::string(home) + "/.metalsharp/games";
    if (!fs::exists(testDir)) return true;

    auto games = metalsharp::GameDetector::detectLocal();
    for (const auto& g : games) {
        assert(g.platform == metalsharp::GamePlatform::Local);
        assert(!g.id.empty());
        assert(g.id.find("local_") == 0);
    }
    return true;
}

static bool crash_reporter_lifecycle() {
    auto& cr = metalsharp::CrashReporter::instance();
    cr.beginSession("TestGame", "/path/to/game.exe");
    cr.endSession(0);
    return true;
}

static bool crash_reporter_generate_id() {
    std::string id1 = metalsharp::CrashReporter::generateId();
    std::string id2 = metalsharp::CrashReporter::generateId();
    assert(!id1.empty());
    assert(!id2.empty());
    assert(id1 != id2);
    return true;
}

static bool crash_reporter_format_timestamp() {
    std::string ts = metalsharp::CrashReporter::formatTimestamp(0);
    assert(!ts.empty());
    return true;
}

static bool crash_reporter_collect_system_info() {
    auto& cr = metalsharp::CrashReporter::instance();
    std::string info = cr.collectSystemInfo();
    assert(!info.empty());
    assert(info.find("Platform:") != std::string::npos);
    assert(info.find("Architecture:") != std::string::npos);
    return true;
}

static bool crash_reporter_save_and_load() {
    auto& cr = metalsharp::CrashReporter::instance();
    cr.beginSession("TestGame2", "/path/to/game2.exe");

    auto report = cr.collectReport();
    assert(!report.id.empty());
    assert(report.collected);
    assert(report.gameName == "TestGame2");
    assert(report.exePath == "/path/to/game2.exe");

    bool saved = cr.saveReport(report);
    assert(saved);

    auto reports = cr.getRecentReports(10);
    bool found = false;
    for (const auto& r : reports) {
        if (r.id == report.id) {
            found = true;
            assert(r.gameName == "TestGame2");
            break;
        }
    }

    if (found) {
        bool deleted = cr.deleteReport(report.id);
        assert(deleted);
    }

    cr.endSession(0);
    return true;
}

static bool crash_reporter_reports_dir() {
    auto& cr = metalsharp::CrashReporter::instance();
    std::string dir = cr.getReportsDir();
    assert(!dir.empty());
    assert(dir.find("metalsharp") != std::string::npos);
    return true;
}

static bool version_parse() {
    auto v = metalsharp::Version::parse("1.2.3");
    assert(v.major == 1);
    assert(v.minor == 2);
    assert(v.patch == 3);
    assert(v.prerelease.empty());

    auto v2 = metalsharp::Version::parse("v2.0.1-beta");
    assert(v2.major == 2);
    assert(v2.minor == 0);
    assert(v2.patch == 1);
    assert(v2.prerelease == "beta");

    auto v3 = metalsharp::Version::parse("0.1.0");
    assert(v3.major == 0);
    assert(v3.minor == 1);
    assert(v3.patch == 0);
    return true;
}

static bool version_comparison() {
    auto v1 = metalsharp::Version::parse("1.0.0");
    auto v2 = metalsharp::Version::parse("2.0.0");
    auto v3 = metalsharp::Version::parse("1.1.0");
    auto v4 = metalsharp::Version::parse("1.0.1");
    auto v5 = metalsharp::Version::parse("1.0.0");

    assert(v2 > v1);
    assert(v3 > v1);
    assert(v4 > v1);
    assert(v1 == v5);
    assert(v1 >= v5);
    assert(v2 >= v1);
    assert(!(v1 > v2));
    return true;
}

static bool version_to_string() {
    auto v = metalsharp::Version::parse("1.2.3");
    assert(v.toString() == "1.2.3");

    auto v2 = metalsharp::Version::parse("2.0.0-alpha");
    assert(v2.toString() == "2.0.0-alpha");
    return true;
}

static bool update_checker_current_version() {
    auto v = metalsharp::UpdateChecker::getCurrentVersion();
    assert(v.major == 0);
    assert(v.minor == 1);
    assert(v.patch == 0);
    return true;
}

static bool update_checker_user_agent() {
    std::string ua = metalsharp::UpdateChecker::getUserAgent();
    assert(!ua.empty());
    assert(ua.find("MetalSharp") != std::string::npos);
    return true;
}

static bool update_checker_parse_release() {
    std::string json = R"({"tag_name":"v0.2.0","html_url":"https://github.com/test/repo/releases/tag/v0.2.0","zipball_url":"https://github.com/test/repo/zipball/v0.2.0","body":"Release notes here"})";

    auto current = metalsharp::Version::parse("0.1.0");
    auto info = metalsharp::UpdateChecker::parseGitHubRelease(json, current);

    assert(info.available);
    assert(info.latestVersion.major == 0);
    assert(info.latestVersion.minor == 2);
    assert(info.latestVersion.patch == 0);
    assert(info.currentVersion == "0.1.0");
    assert(info.releaseNotes == "Release notes here");
    assert(!info.downloadUrl.empty());
    assert(!info.htmlUrl.empty());
    return true;
}

static bool update_checker_no_update() {
    std::string json = R"({"tag_name":"v0.1.0","html_url":"https://github.com/test/repo","zipball_url":"https://github.com/test/repo/zipball","body":""})";

    auto current = metalsharp::Version::parse("0.1.0");
    auto info = metalsharp::UpdateChecker::parseGitHubRelease(json, current);

    assert(!info.available);
    assert(info.latestVersion == current);
    return true;
}

static bool settings_manager_defaults() {
    auto& sm = metalsharp::SettingsManager::instance();
    auto& s = sm.state();
    assert(s.render.renderWidth == 1920);
    assert(s.render.renderHeight == 1080);
    assert(s.render.windowMode == metalsharp::WindowMode::Fullscreen);
    assert(s.render.upscaling == metalsharp::UpscalingQuality::Off);
    assert(s.render.vsync == true);
    assert(s.render.shaderCacheEnabled == true);
    assert(s.render.pipelineCacheEnabled == true);
    assert(s.launchMode == "native");
    return true;
}

static bool settings_manager_string_conversions() {
    using namespace metalsharp;

    assert(SettingsManager::upscalingToString(UpscalingQuality::Off) == "off");
    assert(SettingsManager::upscalingToString(UpscalingQuality::Low) == "low");
    assert(SettingsManager::upscalingToString(UpscalingQuality::Medium) == "medium");
    assert(SettingsManager::upscalingToString(UpscalingQuality::High) == "high");
    assert(SettingsManager::upscalingToString(UpscalingQuality::Ultra) == "ultra");

    assert(SettingsManager::upscalingFromString("off") == UpscalingQuality::Off);
    assert(SettingsManager::upscalingFromString("low") == UpscalingQuality::Low);
    assert(SettingsManager::upscalingFromString("medium") == UpscalingQuality::Medium);
    assert(SettingsManager::upscalingFromString("high") == UpscalingQuality::High);
    assert(SettingsManager::upscalingFromString("ultra") == UpscalingQuality::Ultra);
    assert(SettingsManager::upscalingFromString("unknown") == UpscalingQuality::Off);

    assert(SettingsManager::windowModeToString(WindowMode::Windowed) == "windowed");
    assert(SettingsManager::windowModeToString(WindowMode::Borderless) == "borderless");
    assert(SettingsManager::windowModeToString(WindowMode::Fullscreen) == "fullscreen");

    assert(SettingsManager::windowModeFromString("windowed") == WindowMode::Windowed);
    assert(SettingsManager::windowModeFromString("borderless") == WindowMode::Borderless);
    assert(SettingsManager::windowModeFromString("fullscreen") == WindowMode::Fullscreen);
    assert(SettingsManager::windowModeFromString("unknown") == WindowMode::Fullscreen);
    return true;
}

static bool settings_manager_save_load() {
    auto& sm = metalsharp::SettingsManager::instance();
    auto& s = sm.state();
    s.render.renderWidth = 2560;
    s.render.renderHeight = 1440;
    s.render.windowMode = metalsharp::WindowMode::Borderless;
    s.render.upscaling = metalsharp::UpscalingQuality::High;
    s.render.vsync = false;
    s.launchMode = "wine";

    std::string testPath = "/tmp/metalsharp_test_settings.json";
    bool saved = sm.save(testPath);
    assert(saved);
    assert(fs::exists(testPath));

    s.render.renderWidth = 1920;
    s.render.renderHeight = 1080;
    s.render.windowMode = metalsharp::WindowMode::Fullscreen;
    s.render.upscaling = metalsharp::UpscalingQuality::Off;
    s.render.vsync = true;
    s.launchMode = "native";

    bool loaded = sm.load(testPath);
    assert(loaded);
    assert(s.render.renderWidth == 2560);
    assert(s.render.renderHeight == 1440);
    assert(s.render.windowMode == metalsharp::WindowMode::Borderless);
    assert(s.render.upscaling == metalsharp::UpscalingQuality::High);
    assert(s.render.vsync == false);
    assert(s.launchMode == "wine");

    fs::remove(testPath);

    s.render.renderWidth = 1920;
    s.render.renderHeight = 1080;
    s.render.windowMode = metalsharp::WindowMode::Fullscreen;
    s.render.upscaling = metalsharp::UpscalingQuality::Off;
    s.render.vsync = true;
    s.launchMode = "native";
    return true;
}

static bool settings_manager_default_path() {
    std::string path = metalsharp::SettingsManager::defaultSettingsPath();
    assert(!path.empty());
    assert(path.find("metalsharp") != std::string::npos);
    assert(path.find("settings.json") != std::string::npos);
    return true;
}

int main() {
    printf("\n=== Phase 24: Polish & 1.0 Release ===\n\n");

    printf("--- 24.1 Game Detector ---\n");
    TEST(game_detector_platform_strings);
    TEST(game_detector_platform_from_string);
    TEST(game_detector_detect_local);
    TEST(game_detector_detect_all);
    TEST(game_detector_local_scan);

    printf("\n--- 24.2 Crash Reporter ---\n");
    TEST(crash_reporter_lifecycle);
    TEST(crash_reporter_generate_id);
    TEST(crash_reporter_format_timestamp);
    TEST(crash_reporter_collect_system_info);
    TEST(crash_reporter_save_and_load);
    TEST(crash_reporter_reports_dir);

    printf("\n--- 24.3 Update Checker ---\n");
    TEST(version_parse);
    TEST(version_comparison);
    TEST(version_to_string);
    TEST(update_checker_current_version);
    TEST(update_checker_user_agent);
    TEST(update_checker_parse_release);
    TEST(update_checker_no_update);

    printf("\n--- 24.4 Settings Manager ---\n");
    TEST(settings_manager_defaults);
    TEST(settings_manager_string_conversions);
    TEST(settings_manager_save_load);
    TEST(settings_manager_default_path);

    printf("\n%d/%d passed (%d FAILED)\n\n", g_pass, g_pass + g_fail, g_fail);
    return g_fail > 0 ? 1 : 0;
}
