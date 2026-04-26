#include "WinePrefix.h"
#include "Config.h"
#include "SteamIntegration.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef METALSHARP_VERSION
#define METALSHARP_VERSION "0.1.0"
#endif

#include <getopt.h>

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options] <game.exe | --steam <appid>>\n"
        "\n"
        "Options:\n"
        "  --prefix, -p     Wine prefix directory (default: ~/.metalsharp/prefix)\n"
        "  --width, -W      Window width (default: 1920)\n"
        "  --height, -H     Window height (default: 1080)\n"
        "  --steam, -s      Download and launch Steam game by app ID\n"
        "  --list-games     List Steam library games\n"
        "  --config, -c     Config file path (default: ~/.metalsharp/metalsharp.toml)\n"
        "  --verbose, -v    Verbose output\n"
        "  --debug-metal    Enable Metal validation\n"
        "  --fullscreen, -f Fullscreen mode\n"
        "  --help, -h       Show this help\n"
        "\n"
        "MetalSharp %s — Direct3D -> Metal translation layer\n"
        "Downloads Windows game depots via SteamCMD and runs them through Wine + Metal.\n",
        prog, METALSHARP_VERSION);
}

int main(int argc, char* argv[]) {
    metalsharp::Config config;
    uint32_t steamAppId = 0;
    bool listGames = false;
    std::string configPath;

    static struct option longOpts[] = {
        {"prefix",      required_argument, nullptr, 'p'},
        {"width",       required_argument, nullptr, 'W'},
        {"height",      required_argument, nullptr, 'H'},
        {"steam",       required_argument, nullptr, 's'},
        {"list-games",  no_argument,       nullptr, 'l'},
        {"config",      required_argument, nullptr, 'c'},
        {"verbose",     no_argument,       nullptr, 'v'},
        {"debug-metal", no_argument,       nullptr, 'D'},
        {"fullscreen",  no_argument,       nullptr, 'f'},
        {"help",        no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:W:H:s:lc:vDfh", longOpts, nullptr)) != -1) {
        switch (opt) {
            case 'p': config.winePrefix = optarg; break;
            case 'W': config.width = atoi(optarg); break;
            case 'H': config.height = atoi(optarg); break;
            case 's': steamAppId = (uint32_t)atoi(optarg); break;
            case 'l': listGames = true; break;
            case 'c': configPath = optarg; break;
            case 'v': config.verbose = true; break;
            case 'D': config.debugMetal = true; break;
            case 'f': config.fullscreen = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default: printUsage(argv[0]); return 1;
        }
    }

    if (configPath.empty()) configPath = metalsharp::Config::defaultConfigPath();
    config.load(configPath);

    if (optind < argc) {
        config.executable = argv[optind];
    }

    printf("MetalSharp %s\n", METALSHARP_VERSION);

    if (listGames) {
        std::string steamDir = metalsharp::SteamIntegration::findSteamInstallDir();
        if (steamDir.empty()) {
            fprintf(stderr, "Error: Steam installation not found\n");
            return 1;
        }
        printf("\nSteam library at: %s\n\n", steamDir.c_str());

        auto games = metalsharp::SteamIntegration::enumerateLibrary(steamDir);
        printf("Found %zu games:\n", games.size());
        for (const auto& game : games) {
            printf("  [%u] %s\n", game.appId, game.name.c_str());
        }
        return 0;
    }

    if (steamAppId > 0) {
        std::string steamcmd = metalsharp::SteamIntegration::findSteamCMD();
        if (steamcmd.empty()) {
            fprintf(stderr, "Error: steamcmd not found. Install from https://developer.valvesoftware.com/wiki/SteamCMD\n");
            return 1;
        }

        std::string downloadDir = metalsharp::SteamIntegration::defaultDownloadDir() + "/" + std::to_string(steamAppId);
        printf("Downloading Windows game (app %u)...\n", steamAppId);
        printf("  SteamCMD: %s\n", steamcmd.c_str());
        printf("  Output:   %s\n", downloadDir.c_str());

        if (!metalsharp::SteamIntegration::downloadWindowsGame(steamcmd, steamAppId, downloadDir)) {
            fprintf(stderr, "Error: download failed\n");
            return 1;
        }

        config.executable = metalsharp::SteamIntegration::findGameExecutable(downloadDir);
        if (config.executable.empty()) {
            fprintf(stderr, "Error: no .exe found in download directory\n");
            return 1;
        }
        printf("  Executable: %s\n", config.executable.c_str());
    }

    if (config.executable.empty()) {
        fprintf(stderr, "Error: no executable specified. Use <game.exe> or --steam <appid>\n\n");
        printUsage(argv[0]);
        return 1;
    }

    if (config.winePrefix.empty()) {
        config.winePrefix = metalsharp::WinePrefix::defaultPrefixPath();
    }

    if (config.verbose) {
        printf("\nConfiguration:\n");
        printf("  Prefix:       %s\n", config.winePrefix.c_str());
        printf("  Executable:   %s\n", config.executable.c_str());
        printf("  Resolution:   %ux%u\n", config.width, config.height);
        printf("  Fullscreen:   %s\n", config.fullscreen ? "yes" : "no");
        printf("  Debug Metal:  %s\n", config.debugMetal ? "yes" : "no");
    }

    printf("\nInitializing Wine prefix...\n");
    metalsharp::WinePrefix prefix(config.winePrefix);
    if (!prefix.init()) {
        fprintf(stderr, "Error: failed to initialize wine prefix at %s\n", config.winePrefix.c_str());
        return 1;
    }

    prefix.setDllOverride("d3d11");
    prefix.setDllOverride("d3d12");
    prefix.setDllOverride("dxgi");
    prefix.setDllOverride("xaudio2_9");
    prefix.setDllOverride("xinput1_4");
    prefix.writeRegistryDllOverrides();

    if (!prefix.copyMetalSharpDlls()) {
        fprintf(stderr, "Error: failed to install MetalSharp DLLs\n");
        return 1;
    }

    if (config.debugMetal) {
        setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);
    }

    setenv("WINEPREFIX", config.winePrefix.c_str(), 1);
    setenv("WINEDLLOVERRIDES",
        "d3d11=native;d3d12=native;dxgi=native;xaudio2_9=native;xinput1_4=native", 1);

    printf("\nLaunching %s via MetalSharp...\n", config.executable.c_str());

    std::string launchCmd = "wine \"" + config.executable + "\"";
    if (config.verbose) printf("  Command: %s\n", launchCmd.c_str());

    return system(launchCmd.c_str());
}
