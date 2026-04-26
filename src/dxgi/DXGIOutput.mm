#include <metalsharp/DXGIOutput.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <CoreGraphics/CoreGraphics.h>

namespace metalsharp {

std::vector<DisplayMode> DXGIOutputImpl::enumerateDisplayModes() {
    std::vector<DisplayMode> modes;

    uint32_t displayCount = 0;
    CGGetOnlineDisplayList(0, nullptr, &displayCount);
    if (displayCount == 0) {
        modes.push_back({1920, 1080, 60, 87});
        return modes;
    }

    std::vector<CGDirectDisplayID> displays(displayCount);
    CGGetOnlineDisplayList(displayCount, displays.data(), &displayCount);

    CGDirectDisplayID mainDisplay = displays.empty() ? kCGDirectMainDisplay : displays[0];
    CFArrayRef modeList = CGDisplayCopyAllDisplayModes(mainDisplay, nullptr);
    if (!modeList) {
        modes.push_back({1920, 1080, 60, 87});
        return modes;
    }

    CFIndex count = CFArrayGetCount(modeList);
    for (CFIndex i = 0; i < count; i++) {
        auto* mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modeList, i);
        uint32_t w = (uint32_t)CGDisplayModeGetWidth(mode);
        uint32_t h = (uint32_t)CGDisplayModeGetHeight(mode);
        double refresh = CGDisplayModeGetRefreshRate(mode);
        if (refresh == 0) refresh = 60.0;

        bool dup = false;
        for (auto& m : modes) {
            if (m.width == w && m.height == h && m.refreshRate == (uint32_t)refresh) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            modes.push_back({w, h, (uint32_t)refresh, 87});
        }
    }
    CFRelease(modeList);

    if (modes.empty()) {
        modes.push_back({1920, 1080, 60, 87});
    }

    return modes;
}

DisplayMode DXGIOutputImpl::getCurrentDisplayMode() {
    CGDirectDisplayID mainDisplay = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(mainDisplay);
    if (!mode) return {1920, 1080, 60, 87};

    DisplayMode dm;
    dm.width = (uint32_t)CGDisplayModeGetWidth(mode);
    dm.height = (uint32_t)CGDisplayModeGetHeight(mode);
    dm.refreshRate = (uint32_t)CGDisplayModeGetRefreshRate(mode);
    if (dm.refreshRate == 0) dm.refreshRate = 60;
    dm.format = 87;
    CGDisplayModeRelease(mode);
    return dm;
}

bool DXGIOutputImpl::createWindow(void* parent, uint32_t width, uint32_t height, void** outWindow) {
    @autoreleasepool {
        NSRect frame = NSMakeRect(0, 0, width, height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                  NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable |
                                  NSWindowStyleMaskResizable;

        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];

        [window setTitle:@"MetalSharp"];
        [window center];
        [window makeKeyAndOrderFront:nil];

        if (outWindow) {
            *outWindow = (__bridge_retained void*)window.contentView;
        }
        return true;
    }
}

bool DXGIOutputImpl::createMetalLayer(void* window, void** outLayer) {
    @autoreleasepool {
        NSView* view = (__bridge NSView*)window;
        if (!view) return false;

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.frame = view.bounds;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;

        view.wantsLayer = YES;
        view.layer = layer;

        if (outLayer) {
            *outLayer = (__bridge void*)layer;
        }
        return true;
    }
}

}
