#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma mark - View registry

static CFMutableDictionaryRef s_viewMap = NULL;
static NSInteger s_nextTag = 1000;

static void ensureViewMap(void) {
    if (!s_viewMap) {
        s_viewMap = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    }
}

static NSView* viewForRef(HIViewRef ref) {
    if (!ref) return nil;
    ensureViewMap();
    return (__bridge NSView*)CFDictionaryGetValue(s_viewMap, (const void*)ref);
}

static void associateView(HIViewRef ref, NSView* view) {
    ensureViewMap();
    CFDictionarySetValue(s_viewMap, (const void*)ref, (__bridge const void*)(view));
}

static HIViewRef refForView(NSView* view) {
    ensureViewMap();
    NSInteger tag = [view tag];
    if (tag < 1000) {
        tag = s_nextTag++;
        [view setTag:tag];
    }
    return (HIViewRef)(intptr_t)tag;
}

#pragma mark - HIView shims (reimplemented for 64-bit via NSView)

__attribute__((visibility("default")))
OSStatus HIViewPlaceInSuperviewAt(HIViewRef inView, float x, float y) {
    NSView* view = viewForRef(inView);
    if (view) {
        NSPoint origin = NSMakePoint(x, y);
        if (view.superview) {
            origin.y = view.superview.bounds.size.height - y - view.frame.size.height;
        }
        [view setFrameOrigin:origin];
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewAddSubview(HIViewRef inParent, HIViewRef inChild) {
    NSView* parent = viewForRef(inParent);
    NSView* child = viewForRef(inChild);
    if (parent && child) {
        [parent addSubview:child];
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewRemoveFromSuperview(HIViewRef inView) {
    NSView* view = viewForRef(inView);
    if (view) {
        [view removeFromSuperview];
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewSetFrame(HIViewRef inView, const HIRect* inRect) {
    NSView* view = viewForRef(inView);
    if (view && inRect) {
        CGFloat flippedY = inRect->origin.y;
        if (view.superview) {
            flippedY = view.superview.bounds.size.height - inRect->origin.y - inRect->size.height;
        }
        [view setFrame:NSMakeRect(inRect->origin.x, flippedY, inRect->size.width, inRect->size.height)];
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewGetFrame(HIViewRef inView, HIRect* outRect) {
    NSView* view = viewForRef(inView);
    if (view && outRect) {
        NSRect frame = view.frame;
        if (view.superview) {
            frame.origin.y = view.superview.bounds.size.height - frame.origin.y - frame.size.height;
        }
        *outRect = CGRectMake(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewGetBounds(HIViewRef inView, HIRect* outBounds) {
    NSView* view = viewForRef(inView);
    if (view && outBounds) {
        NSRect bounds = view.bounds;
        *outBounds = CGRectMake(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
    }
    return noErr;
}

__attribute__((visibility("default")))
HIViewRef HIViewGetSuperview(HIViewRef inView) {
    NSView* view = viewForRef(inView);
    if (view && view.superview) return refForView(view.superview);
    return NULL;
}

__attribute__((visibility("default")))
WindowRef HIViewGetWindow(HIViewRef inView) {
    NSView* view = viewForRef(inView);
    if (view && view.window) return (WindowRef)(__bridge void*)view.window;
    return NULL;
}

__attribute__((visibility("default")))
Boolean HIViewIsVisible(HIViewRef inView) {
    NSView* view = viewForRef(inView);
    return view ? !view.isHidden : false;
}

__attribute__((visibility("default")))
OSStatus HIViewSetVisible(HIViewRef inView, Boolean inVisible) {
    NSView* view = viewForRef(inView);
    if (view) [view setHidden:!inVisible];
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewSetZOrder(HIViewRef inView, HIViewZOrderOp inOrder, HIViewRef inOtherView) {
    NSView* view = viewForRef(inView);
    NSView* other = viewForRef(inOtherView);
    if (view && view.superview) {
        NSView* sv = view.superview;
        [view removeFromSuperview];
        if (inOrder == kHIViewZOrderAbove && other) {
            [sv insertSubview:view aboveSubview:other];
        } else if (inOrder == kHIViewZOrderBelow && other) {
            [sv insertSubview:view belowSubview:other];
        } else if (inOrder == 1) {
            [sv insertSubview:view atIndex:0];
        } else {
            [sv addSubview:view];
        }
    }
    return noErr;
}

__attribute__((visibility("default")))
HIViewRef HIViewGetRoot(WindowRef inWindow) {
    if (!inWindow) return NULL;
    ensureViewMap();
    NSView* contentView = (__bridge NSView*)CFDictionaryGetValue(s_viewMap, (const void*)inWindow);
    if (contentView) return refForView(contentView);
    id obj = (__bridge id)inWindow;
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSView* cv = [(NSWindow*)obj contentView];
        if (cv) return refForView(cv);
    }
    return NULL;
}

__attribute__((visibility("default")))
OSStatus HIViewFindByID(HIViewRef inStartView, HIViewID inID, HIViewRef* outView) {
    (void)inStartView; (void)inID;
    if (outView) *outView = NULL;
    return -30580;
}

__attribute__((visibility("default")))
HIViewRef HIViewGetPreviousView(HIViewRef inView) {
    NSView* view = viewForRef(inView);
    if (view && view.superview) {
        NSArray* siblings = view.superview.subviews;
        NSUInteger idx = [siblings indexOfObject:view];
        if (idx > 0 && idx != NSNotFound) {
            return refForView(siblings[idx - 1]);
        }
    }
    return NULL;
}

__attribute__((visibility("default")))
OSStatus HIViewConvertPoint(HIViewRef inView, HIPoint* ioPoint, HIViewRef inDestView) {
    if (!ioPoint) return paramErr;
    NSView* src = viewForRef(inView);
    NSView* dst = viewForRef(inDestView);
    if (src) {
        NSPoint pt = [src convertPoint:NSMakePoint(ioPoint->x, ioPoint->y) toView:dst];
        ioPoint->x = pt.x;
        ioPoint->y = pt.y;
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewScrollRect(HIViewRef inView, const HIRect* inRect, float inDX, float inDY) {
    NSView* view = viewForRef(inView);
    if (view && inRect) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [view scrollRect:NSMakeRect(inRect->origin.x, inRect->origin.y, inRect->size.width, inRect->size.height)
                     by:NSMakeSize(inDX, inDY)];
#pragma clang diagnostic pop
    }
    return noErr;
}

__attribute__((visibility("default")))
HIViewRef HIViewGetSubviewHit(HIViewRef inView, HIPoint inPoint, Boolean inDeep) {
    (void)inDeep;
    NSView* view = viewForRef(inView);
    if (view) {
        NSView* hit = [view hitTest:NSMakePoint(inPoint.x, inPoint.y)];
        if (hit && hit != view) return refForView(hit);
    }
    return NULL;
}

__attribute__((visibility("default")))
OSStatus HIViewChangeFeatures(HIViewRef inView, HIViewFeatures inSet, HIViewFeatures inClear) {
    (void)inView; (void)inSet; (void)inClear;
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewSetNeedsDisplayInRect(HIViewRef inView, const HIRect* inRect, Boolean inNeedsDisplay) {
    NSView* view = viewForRef(inView);
    if (view && inNeedsDisplay) {
        if (inRect) {
            [view setNeedsDisplayInRect:NSMakeRect(inRect->origin.x, inRect->origin.y, inRect->size.width, inRect->size.height)];
        } else {
            [view setNeedsDisplay:YES];
        }
    }
    return noErr;
}

__attribute__((visibility("default")))
OSStatus HIViewNewTrackingArea(HIViewRef inView, const HIRect* inRect, HIViewTrackingAreaID inID, OptionBits inOpts, HIViewTrackingAreaRef* outRef) {
    (void)inView; (void)inRect; (void)inID; (void)inOpts;
    if (outRef) *outRef = NULL;
    return noErr;
}

#pragma mark - HIObject shims

__attribute__((visibility("default")))
OSStatus HIObjectCreate(CFStringRef inClassID, EventRef inConstructData, HIObjectRef* outObject) {
    (void)inClassID; (void)inConstructData;
    if (!outObject) return paramErr;
    NSObject* obj = [[NSObject alloc] init];
    *outObject = (__bridge HIObjectRef)obj;
    return noErr;
}
