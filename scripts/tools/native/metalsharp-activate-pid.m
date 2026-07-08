// metalsharp-activate-pid
// Bring the window(s) of a process to front by PID.
// Usage: metalsharp-activate-pid <pid>
// Build: clang -framework Cocoa -o metalsharp-activate-pid metalsharp-activate-pid.m

#import <Cocoa/Cocoa.h>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    @autoreleasepool {
        int pid = atoi(argv[1]);
        if (pid <= 0) return 1;
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (app) {
            [app activateWithOptions:NSApplicationActivateAllWindows];
            return 0;
        }
    }
    return 1;
}
