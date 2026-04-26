#include "WinePrefix.h"
#include "Config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef METALSHARP_VERSION
#define METALSHARP_VERSION "0.1.0"
#endif

#ifdef __APPLE__
#include <getopt.h>
#endif

static void printUsage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options] <game.exe>\n"
        "\n"
        "Options:\n"
        "  --prefix, -p   Wine prefix directory (default: ~/.metalsharp/prefix)\n"
        "  --width, -W    Window width (default: 1920)\n"
        "  --height, -H   Window height (default: 1080)\n"
        "  --verbose, -v  Verbose output\n"
        "  --debug-metal  Enable Metal validation\n"
        "  --help, -h     Show this help\n"
        "\n"
        "MetalSharp %s — Direct3D → Metal translation layer\n",
        prog, METALSHARP_VERSION);
}

int main(int argc, char* argv[]) {
    metalsharp::Config config;

    static struct option longOpts[] = {
        {"prefix",      required_argument, nullptr, 'p'},
        {"width",       required_argument, nullptr, 'W'},
        {"height",      required_argument, nullptr, 'H'},
        {"verbose",     no_argument,       nullptr, 'v'},
        {"debug-metal", no_argument,       nullptr, 'D'},
        {"help",        no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:W:H:vDh", longOpts, nullptr)) != -1) {
        switch (opt) {
            case 'p': config.winePrefix = optarg; break;
            case 'W': config.width = atoi(optarg); break;
            case 'H': config.height = atoi(optarg); break;
            case 'v': config.verbose = true; break;
            case 'D': config.debugMetal = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default: printUsage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: no executable specified\n\n");
        printUsage(argv[0]);
        return 1;
    }

    config.executable = argv[optind];

    if (config.winePrefix.empty()) {
        const char* home = getenv("HOME");
        if (home) config.winePrefix = std::string(home) + "/.metalsharp/prefix";
    }

    if (config.verbose) {
        printf("MetalSharp %s\n", METALSHARP_VERSION);
        printf("  Prefix:     %s\n", config.winePrefix.c_str());
        printf("  Executable: %s\n", config.executable.c_str());
        printf("  Resolution: %ux%u\n", config.width, config.height);
        printf("  Debug Metal: %s\n", config.debugMetal ? "yes" : "no");
    }

    metalsharp::WinePrefix prefix(config.winePrefix);
    if (!prefix.init()) {
        fprintf(stderr, "Error: failed to initialize wine prefix at %s\n", config.winePrefix.c_str());
        return 1;
    }

    if (!prefix.copyMetalSharpDlls()) {
        fprintf(stderr, "Error: failed to install MetalSharp DLLs\n");
        return 1;
    }

    prefix.setDllOverride("d3d11");
    prefix.setDllOverride("d3d12");
    prefix.setDllOverride("dxgi");
    prefix.setDllOverride("xaudio2_9");
    prefix.setDllOverride("xinput1_4");

    if (config.debugMetal) {
        setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);
    }

    printf("Launching %s via MetalSharp...\n", config.executable.c_str());

    return 0;
}
