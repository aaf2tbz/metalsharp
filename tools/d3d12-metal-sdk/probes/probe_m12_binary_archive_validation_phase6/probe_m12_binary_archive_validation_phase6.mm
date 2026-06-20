#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

struct ProbeArchiveContext {
    std::mutex mutex;
    id<MTLBinaryArchive> archive = nil;
    const char* native_path = nullptr;
    bool enabled = false;
    bool allow_lookup = false;
    bool existing_archive_has_bytes = false;
    bool loaded_existing_archive = false;
    bool used_memory_fallback = false;
    bool validation_passed = false;
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

static bool regularFileHasBytes(const char* path) {
    if (!path || !path[0])
        return false;
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static unsigned long long fileSize(const char* path) {
    struct stat st = {};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;
    return (unsigned long long)st.st_size;
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

static NSString* shaderSource(void) {
    return @"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "kernel void m12_binary_archive_validation_cs(uint id [[thread_position_in_grid]]) { }\n";
}

static id<MTLBinaryArchive> newArchive(id<MTLDevice> device, const char* path_or_null) {
    MTLBinaryArchiveDescriptor* desc = [[MTLBinaryArchiveDescriptor alloc] init];
    if (path_or_null)
        desc.url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path_or_null]];
    NSError* err = nil;
    id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
    [desc release];
    return archive;
}

static bool validateBinaryArchiveLookupSupport(id<MTLDevice> device, const char* validation_path,
                                               bool force_serialize_failure) {
    unlink(validation_path);
    std::string tmp = std::string(validation_path) + ".tmp";
    unlink(tmp.c_str());

    id<MTLBinaryArchive> archive = newArchive(device, nullptr);
    if (!archive)
        return false;

    NSError* err = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:shaderSource() options:nil error:&err];
    if (!library || err)
        return false;
    id<MTLFunction> function = [library newFunctionWithName:@"m12_binary_archive_validation_cs"];
    if (!function)
        return false;

    MTLComputePipelineDescriptor* desc = [[MTLComputePipelineDescriptor alloc] init];
    desc.computeFunction = function;
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithDescriptor:desc
                                                                                 options:MTLPipelineOptionNone
                                                                              reflection:nil
                                                                                   error:&err];
    if (!pipeline || err)
        return false;

    bool add_ok = true;
    @synchronized(archive) {
        @try {
            [archive addComputePipelineFunctionsWithDescriptor:desc error:nil];
        } @catch (NSException* exception) {
            (void)exception;
            add_ok = false;
        }
    }
    if (!add_ok)
        return false;

    if (!force_serialize_failure)
        [archive serializeToURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:validation_path]] error:nil];

    bool ok = regularFileHasBytes(validation_path);
    unlink(validation_path);
    unlink(tmp.c_str());
    [pipeline release];
    [desc release];
    [function release];
    [library release];
    [archive release];
    return ok;
}

static bool createGoodArchive(id<MTLDevice> device, const char* archive_path) {
    unlink(archive_path);
    rmdir(archive_path);
    id<MTLBinaryArchive> archive = newArchive(device, nullptr);
    if (!archive)
        return false;
    NSError* err = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:shaderSource() options:nil error:&err];
    if (!library || err)
        return false;
    id<MTLFunction> function = [library newFunctionWithName:@"m12_binary_archive_validation_cs"];
    if (!function)
        return false;
    MTLComputePipelineDescriptor* desc = [[MTLComputePipelineDescriptor alloc] init];
    desc.computeFunction = function;
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithDescriptor:desc
                                                                                 options:MTLPipelineOptionNone
                                                                              reflection:nil
                                                                                   error:&err];
    if (!pipeline || err)
        return false;
    [archive addComputePipelineFunctionsWithDescriptor:desc error:nil];
    [archive serializeToURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:archive_path]] error:nil];
    bool ok = regularFileHasBytes(archive_path);
    [pipeline release];
    [desc release];
    [function release];
    [library release];
    [archive release];
    return ok;
}

static void initializeContext(ProbeArchiveContext& ctx, id<MTLDevice> device, bool force_validation_failure,
                              const char* validation_path) {
    std::lock_guard<std::mutex> lock(ctx.mutex);
    if (!envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE"))
        return;

    ctx.native_path = formatArchivePath(device);
    const bool bypass_lookup = envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP");
    ctx.allow_lookup = false;

    char parent[1024] = {};
    snprintf(parent, sizeof(parent), "%s", ctx.native_path ? ctx.native_path : "");
    char* slash = strrchr(parent, '/');
    char* backslash = strrchr(parent, '\\');
    char* last = slash && backslash ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);
    if (last) {
        *last = '\0';
        ensureDirectoryBestEffort(parent);
    }

    ctx.existing_archive_has_bytes = regularFileHasBytes(ctx.native_path);
    if (ctx.native_path && access(ctx.native_path, F_OK) == 0 && ctx.existing_archive_has_bytes) {
        ctx.archive = newArchive(device, ctx.native_path);
        ctx.loaded_existing_archive = ctx.archive != nil;
    }
    if (!ctx.archive) {
        ctx.archive = newArchive(device, nullptr);
        ctx.used_memory_fallback = ctx.archive != nil;
    }
    if (!ctx.archive) {
        ctx.native_path = nullptr;
        ctx.allow_lookup = false;
        return;
    }

    ctx.validation_passed = validateBinaryArchiveLookupSupport(device, validation_path, force_validation_failure);
    ctx.allow_lookup = ctx.validation_passed && ctx.loaded_existing_archive && !bypass_lookup;
    ctx.enabled = true;
}

static bool writeTextFile(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        const char* case_name = "good";
        const char* output_path = nullptr;
        const char* validation_path = "/tmp/m12_test.bin";
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--case") && i + 1 < argc)
                case_name = argv[++i];
            else if (!strcmp(argv[i], "--output") && i + 1 < argc)
                output_path = argv[++i];
            else if (!strcmp(argv[i], "--validation-path") && i + 1 < argc)
                validation_path = argv[++i];
        }
        if (!output_path)
            return 2;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return 1;

        const char* expected_path = formatArchivePath(device);
        char parent[1024] = {};
        snprintf(parent, sizeof(parent), "%s", expected_path);
        char* slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            ensureDirectoryBestEffort(parent);
        }

        unlink(expected_path);
        rmdir(expected_path);
        bool prepared_good = false;
        bool prepared_corrupt = false;
        bool prepared_empty = false;
        bool force_validation_failure = false;
        if (!strcmp(case_name, "good") || !strcmp(case_name, "bypass")) {
            prepared_good = createGoodArchive(device, expected_path);
        } else if (!strcmp(case_name, "corrupt")) {
            prepared_corrupt = writeTextFile(expected_path, "not-a-metal-binary-archive\n");
        } else if (!strcmp(case_name, "empty")) {
            prepared_empty = writeTextFile(expected_path, "");
        } else if (!strcmp(case_name, "validation-failure")) {
            prepared_good = createGoodArchive(device, expected_path);
            force_validation_failure = true;
        }

        ProbeArchiveContext ctx;
        initializeContext(ctx, device, force_validation_failure, validation_path);

        const bool population_allowed = ctx.enabled && ctx.archive != nil && ctx.native_path != nullptr;
        const bool lookup_disabled_but_population_allowed = !ctx.allow_lookup && population_allowed;

        NSDictionary* out = @{
            @"case" : [NSString stringWithUTF8String:case_name],
            @"env_enable_exact_one" : @(envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE")),
            @"env_bypass_exact_one" : @(envSwitchOne("DXMT_D3D12_BINARY_ARCHIVE_BYPASS_LOOKUP")),
            @"enabled" : @(ctx.enabled),
            @"allow_lookup" : @(ctx.allow_lookup),
            @"archive_handle_nonzero" : @(ctx.archive != nil),
            @"population_allowed" : @(population_allowed),
            @"lookup_disabled_but_population_allowed" : @(lookup_disabled_but_population_allowed),
            @"existing_archive_has_bytes" : @(ctx.existing_archive_has_bytes),
            @"loaded_existing_archive" : @(ctx.loaded_existing_archive),
            @"used_memory_fallback" : @(ctx.used_memory_fallback),
            @"validation_passed" : @(ctx.validation_passed),
            @"prepared_good_archive" : @(prepared_good),
            @"prepared_corrupt_archive" : @(prepared_corrupt),
            @"prepared_empty_archive" : @(prepared_empty),
            @"native_path" : ctx.native_path ? [NSString stringWithUTF8String:ctx.native_path] : @"",
            @"expected_path" : [NSString stringWithUTF8String:expected_path],
            @"expected_path_size" : @(fileSize(expected_path)),
        };
        NSData* json = [NSJSONSerialization dataWithJSONObject:out
                                                       options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                         error:nil];
        if (!json || ![json writeToFile:[NSString stringWithUTF8String:output_path] atomically:YES])
            return 1;
        return 0;
    }
}
