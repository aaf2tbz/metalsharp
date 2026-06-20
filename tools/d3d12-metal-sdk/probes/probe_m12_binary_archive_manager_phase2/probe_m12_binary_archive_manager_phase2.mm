#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

struct ProbeArchiveContext {
    std::mutex mutex;
    id<MTLBinaryArchive> archive = nil;
    const char* native_path = nullptr;
    bool enabled = false;
    bool allow_lookup = false;
    bool used_path_load = false;
    bool used_memory_fallback = false;
};

static bool envSwitchOne(const char* name) {
    const char* value = getenv(name);
    return value && strcmp(value, "1") == 0;
}

static std::string envPath(const char* name) {
    const char* value = getenv(name);
    if (!value || !value[0])
        return {};
    std::string path(value);
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    return path;
}

static void ensureDirectoryBestEffort(const char* path) {
    if (!path || !path[0])
        return;
    char partial[1024] = {};
    snprintf(partial, sizeof(partial), "%s", path);
    size_t len = strlen(partial);
    while (len > 1 && (partial[len - 1] == '/' || partial[len - 1] == '\\'))
        partial[--len] = '\0';
    for (char* p = partial + 1; *p; p++) {
        if (*p != '/' && *p != '\\')
            continue;
        char saved = *p;
        *p = '\0';
        if (partial[0])
            mkdir(partial, 0755);
        *p = saved;
    }
    mkdir(partial, 0755);
}

static const char* formatArchivePath(id<MTLDevice> device) {
    std::string root = envPath("DXMT_PIPELINE_CACHE_PATH");
    if (root.empty())
        root = envPath("DXMT_SHADER_CACHE_PATH");
    if (root.empty())
        root = "/tmp/dxmt_shader_cache";

    char path[1024] = {};
    snprintf(path, sizeof(path), "%s/m12-metal-binary-archive-%016llx.binarchive", root.c_str(),
             (unsigned long long)[device registryID]);
    return strdup(path);
}

static void initializeContext(ProbeArchiveContext& ctx, id<MTLDevice> device) {
    std::lock_guard<std::mutex> lock(ctx.mutex);
    if (!envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE"))
        return;

    ctx.native_path = formatArchivePath(device);
    ctx.allow_lookup = !envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP");

    char parent[1024] = {};
    snprintf(parent, sizeof(parent), "%s", ctx.native_path ? ctx.native_path : "");
    char* slash = strrchr(parent, '/');
    char* backslash = strrchr(parent, '\\');
    char* last = slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    if (last) {
        *last = '\0';
        ensureDirectoryBestEffort(parent);
    }

    NSError* error = nil;
    if (ctx.native_path && access(ctx.native_path, F_OK) == 0) {
        MTLBinaryArchiveDescriptor* desc = [[MTLBinaryArchiveDescriptor alloc] init];
        desc.url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:ctx.native_path]];
        ctx.archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
        ctx.used_path_load = ctx.archive != nil;
        [desc release];
    }
    if (!ctx.archive) {
        error = nil;
        MTLBinaryArchiveDescriptor* desc = [[MTLBinaryArchiveDescriptor alloc] init];
        ctx.archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
        ctx.used_memory_fallback = ctx.archive != nil;
        [desc release];
    }
    if (!ctx.archive) {
        ctx.native_path = nullptr;
        ctx.allow_lookup = false;
        return;
    }
    ctx.enabled = true;
}

static ProbeArchiveContext& getContext(id<MTLDevice> device) {
    static ProbeArchiveContext ctx;
    static std::once_flag once;
    std::call_once(once, initializeContext, std::ref(ctx), device);
    return ctx;
}

static bool copyFile(const char* src, const char* dst) {
    NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:src]];
    if (!data)
        return false;
    return [data writeToFile:[NSString stringWithUTF8String:dst] atomically:YES];
}

int main(int argc, char** argv) {
    @autoreleasepool {
        const char* case_name = "enabled";
        const char* output_path = nullptr;
        const char* existing_archive = nullptr;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--case") && i + 1 < argc)
                case_name = argv[++i];
            else if (!strcmp(argv[i], "--output") && i + 1 < argc)
                output_path = argv[++i];
            else if (!strcmp(argv[i], "--existing-archive") && i + 1 < argc)
                existing_archive = argv[++i];
        }
        if (!output_path)
            return 2;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return 1;

        const char* expected_path = formatArchivePath(device);
        bool prepared_existing = false;
        bool prepared_corrupt = false;
        if (!strcmp(case_name, "existing") && existing_archive) {
            char parent[1024] = {};
            snprintf(parent, sizeof(parent), "%s", expected_path);
            char* slash = strrchr(parent, '/');
            if (slash) {
                *slash = '\0';
                ensureDirectoryBestEffort(parent);
            }
            unlink(expected_path);
            prepared_existing = copyFile(existing_archive, expected_path);
        } else if (!strcmp(case_name, "corrupt")) {
            char parent[1024] = {};
            snprintf(parent, sizeof(parent), "%s", expected_path);
            char* slash = strrchr(parent, '/');
            if (slash) {
                *slash = '\0';
                ensureDirectoryBestEffort(parent);
            }
            unlink(expected_path);
            rmdir(expected_path);
            prepared_corrupt = mkdir(expected_path, 0755) == 0;
        } else if (!strcmp(case_name, "missing")) {
            unlink(expected_path);
            rmdir(expected_path);
        }

        ProbeArchiveContext& ctx = getContext(device);
        const char* first_path = ctx.native_path;
        ProbeArchiveContext& second_ctx = getContext(device);
        const char* second_path = second_ctx.native_path;

        NSDictionary* out = @{
            @"case" : [NSString stringWithUTF8String:case_name],
            @"env_enable_exact_one" : @(envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE")),
            @"env_bypass_exact_one" : @(envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP")),
            @"enabled" : @(ctx.enabled),
            @"allow_lookup" : @(ctx.allow_lookup),
            @"archive_handle_nonzero" : @(ctx.archive != nil),
            @"native_path" : ctx.native_path ? [NSString stringWithUTF8String:ctx.native_path] : @"",
            @"expected_path" : [NSString stringWithUTF8String:expected_path],
            @"path_formatted_once_process_lifetime" : @(first_path == second_path),
            @"used_path_load" : @(ctx.used_path_load),
            @"used_memory_fallback" : @(ctx.used_memory_fallback),
            @"prepared_existing_archive" : @(prepared_existing),
            @"prepared_corrupt_archive" : @(prepared_corrupt),
        };
        NSData* json = [NSJSONSerialization dataWithJSONObject:out
                                                       options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                         error:nil];
        if (!json || ![json writeToFile:[NSString stringWithUTF8String:output_path] atomically:YES])
            return 1;
        return 0;
    }
}
