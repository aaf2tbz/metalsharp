#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <pthread.h>
#include <thread>
#include <vector>

static constexpr uint64_t kSerializeInterval = 256;
static pthread_mutex_t g_m12_binary_archive_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::atomic<int> g_active_archive_mutators{0};
static std::atomic<int> g_max_active_archive_mutators{0};

struct ProbeContext {
    id<MTLBinaryArchive> archive = nil;
    NSURL* url = nil;
    std::atomic<uint64_t> counter{0};
    std::atomic<uint64_t> serializations{0};
};

static void EnterMutator(void) {
    int active = g_active_archive_mutators.fetch_add(1, std::memory_order_relaxed) + 1;
    int observed = g_max_active_archive_mutators.load(std::memory_order_relaxed);
    while (active > observed && !g_max_active_archive_mutators.compare_exchange_weak(
                                    observed, active, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

static void LeaveMutator(void) {
    g_active_archive_mutators.fetch_sub(1, std::memory_order_relaxed);
}

static NSString* ShaderSource(void) {
    return @"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "kernel void cs_main(uint id [[thread_position_in_grid]]) { }\n"
            "vertex float4 vs_main(uint vid [[vertex_id]]) {\n"
            "  float2 p[3] = { float2(-1.0, -1.0), float2(0.0, 1.0), float2(1.0, -1.0) };\n"
            "  return float4(p[vid], 0.0, 1.0);\n"
            "}\n"
            "fragment float4 ps_main() { return float4(1.0, 0.0, 1.0, 1.0); }\n";
}

static bool WriteFile(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

static unsigned long long FileSize(NSString* path) {
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    if (!attrs)
        return 0;
    return [[attrs objectForKey:NSFileSize] unsignedLongLongValue];
}

static id<MTLBinaryArchive> NewArchive(id<MTLDevice> device, NSURL* url, bool load_from_disk) {
    MTLBinaryArchiveDescriptor* desc = [[MTLBinaryArchiveDescriptor alloc] init];
    if (load_from_disk)
        desc.url = url;
    NSError* err = nil;
    id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
    [desc release];
    return archive;
}

static MTLComputePipelineDescriptor* NewComputeDescriptor(id<MTLFunction> cs, NSArray* archives) {
    MTLComputePipelineDescriptor* desc = [[MTLComputePipelineDescriptor alloc] init];
    desc.computeFunction = cs;
    desc.binaryArchives = archives;
    return desc;
}

static MTLRenderPipelineDescriptor* NewRenderDescriptor(id<MTLFunction> vs, id<MTLFunction> ps, NSArray* archives) {
    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs;
    desc.fragmentFunction = ps;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.binaryArchives = archives;
    return desc;
}

static bool SafeAddCompute(id<MTLBinaryArchive> archive, MTLComputePipelineDescriptor* desc) {
    bool ok = true;
    @synchronized(archive) {
        pthread_mutex_lock(&g_m12_binary_archive_mutex);
        EnterMutator();
        @try {
            [archive addComputePipelineFunctionsWithDescriptor:desc error:nil];
        } @catch (NSException* exception) {
            (void)exception;
            desc.binaryArchives = nil;
            ok = false;
        } @finally {
            LeaveMutator();
            pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        }
    }
    return ok;
}

static bool SafeAddRender(id<MTLBinaryArchive> archive, MTLRenderPipelineDescriptor* desc) {
    bool ok = true;
    @synchronized(archive) {
        pthread_mutex_lock(&g_m12_binary_archive_mutex);
        EnterMutator();
        @try {
            [archive addRenderPipelineFunctionsWithDescriptor:desc error:nil];
        } @catch (NSException* exception) {
            (void)exception;
            desc.binaryArchives = nil;
            ok = false;
        } @finally {
            LeaveMutator();
            pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        }
    }
    return ok;
}

static bool SafeSerialize(ProbeContext& ctx) {
    bool ok = true;
    @synchronized(ctx.archive) {
        pthread_mutex_lock(&g_m12_binary_archive_mutex);
        EnterMutator();
        @try {
            NSString* path = [ctx.url path];
            NSString* tmpPath = [path stringByAppendingString:@".tmp"];
            NSURL* tmpUrl = [NSURL fileURLWithPath:tmpPath];
            [[NSFileManager defaultManager] removeItemAtPath:tmpPath error:nil];
            [ctx.archive serializeToURL:tmpUrl error:nil];
            if (FileSize(tmpPath) > 0) {
                rename([tmpPath fileSystemRepresentation], [path fileSystemRepresentation]);
                ctx.serializations.fetch_add(1, std::memory_order_relaxed);
            } else {
                ok = false;
            }
        } @catch (NSException* exception) {
            (void)exception;
            ok = false;
        } @finally {
            LeaveMutator();
            pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        }
    }
    return ok;
}

static void RecordOpportunity(ProbeContext& ctx) {
    uint64_t current_count = ctx.counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((current_count % kSerializeInterval) == 0)
        SafeSerialize(ctx);
}

static bool CreateBaselinePipelines(id<MTLDevice> device, MTLComputePipelineDescriptor* compute_desc,
                                    MTLRenderPipelineDescriptor* render_desc) {
    NSError* err = nil;
    id<MTLComputePipelineState> compute = [device newComputePipelineStateWithDescriptor:compute_desc
                                                                                options:MTLPipelineOptionNone
                                                                             reflection:nil
                                                                                  error:&err];
    if (!compute || err)
        return false;
    [compute release];
    err = nil;
    id<MTLRenderPipelineState> render = [device newRenderPipelineStateWithDescriptor:render_desc
                                                                             options:MTLPipelineOptionNone
                                                                          reflection:nil
                                                                               error:&err];
    if (!render || err)
        return false;
    [render release];
    return true;
}

static bool StrictLookupPass(id<MTLDevice> device, NSURL* url, id<MTLFunction> cs, id<MTLFunction> vs,
                             id<MTLFunction> ps) {
    id<MTLBinaryArchive> reloaded = NewArchive(device, url, true);
    if (!reloaded)
        return false;
    NSArray* archives = [NSArray arrayWithObject:reloaded];
    MTLComputePipelineDescriptor* compute_desc = NewComputeDescriptor(cs, archives);
    MTLRenderPipelineDescriptor* render_desc = NewRenderDescriptor(vs, ps, archives);
    NSError* err = nil;
    id<MTLComputePipelineState> compute =
        [device newComputePipelineStateWithDescriptor:compute_desc
                                              options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                           reflection:nil
                                                error:&err];
    bool compute_ok = compute && !err;
    if (compute)
        [compute release];
    err = nil;
    id<MTLRenderPipelineState> render =
        [device newRenderPipelineStateWithDescriptor:render_desc
                                             options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                          reflection:nil
                                               error:&err];
    bool render_ok = render && !err;
    if (render)
        [render release];
    [compute_desc release];
    [render_desc release];
    [reloaded release];
    return compute_ok && render_ok;
}

static int RunProbe(const char* output, const char* archive_path) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSString* archivePath = [NSString stringWithUTF8String:archive_path];
    [[NSFileManager defaultManager] removeItemAtPath:archivePath error:nil];
    NSURL* url = [NSURL fileURLWithPath:archivePath];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return 2;

    NSError* err = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:ShaderSource() options:nil error:&err];
    if (!library || err)
        return 3;
    id<MTLFunction> cs = [library newFunctionWithName:@"cs_main"];
    id<MTLFunction> vs = [library newFunctionWithName:@"vs_main"];
    id<MTLFunction> ps = [library newFunctionWithName:@"ps_main"];
    if (!cs || !vs || !ps)
        return 4;

    ProbeContext ctx;
    ctx.archive = NewArchive(device, url, false);
    ctx.url = url;
    if (!ctx.archive)
        return 5;

    NSArray* archives = [NSArray arrayWithObject:ctx.archive];
    MTLComputePipelineDescriptor* compute_desc = NewComputeDescriptor(cs, archives);
    MTLRenderPipelineDescriptor* render_desc = NewRenderDescriptor(vs, ps, archives);
    const bool add_compute_ok = SafeAddCompute(ctx.archive, compute_desc);
    const bool add_render_ok = SafeAddRender(ctx.archive, render_desc);
    const bool baseline_ok = CreateBaselinePipelines(device, compute_desc, render_desc);

    for (uint64_t i = 0; i < kSerializeInterval - 1; i++)
        RecordOpportunity(ctx);
    const unsigned long long before_threshold_size = FileSize(archivePath);
    const uint64_t serializations_before_threshold = ctx.serializations.load(std::memory_order_relaxed);
    RecordOpportunity(ctx);
    const uint64_t serializations_after_threshold = ctx.serializations.load(std::memory_order_relaxed);
    const unsigned long long archive_size = FileSize(archivePath);

    std::thread add_thread([&]() {
        for (int i = 0; i < 8; i++) {
            SafeAddCompute(ctx.archive, compute_desc);
            SafeAddRender(ctx.archive, render_desc);
        }
    });
    std::thread serialize_thread([&]() {
        for (int i = 0; i < 8; i++)
            SafeSerialize(ctx);
    });
    add_thread.join();
    serialize_thread.join();

    const bool strict_lookup_ok = StrictLookupPass(device, url, cs, vs, ps);
    const int max_active = g_max_active_archive_mutators.load(std::memory_order_relaxed);
    const bool passed = add_compute_ok && add_render_ok && baseline_ok && serializations_before_threshold == 0 &&
                        before_threshold_size == 0 && serializations_after_threshold == 1 && archive_size > 0 &&
                        strict_lookup_ok && max_active == 1;

    char buffer[4096];
    snprintf(buffer, sizeof(buffer),
             "{\n"
             "  \"add_compute_ok\": %s,\n"
             "  \"add_render_ok\": %s,\n"
             "  \"baseline_pso_creation_ok\": %s,\n"
             "  \"serializations_before_threshold\": %llu,\n"
             "  \"before_threshold_size\": %llu,\n"
             "  \"serializations_after_threshold\": %llu,\n"
             "  \"archive_size\": %llu,\n"
             "  \"strict_lookup_ok\": %s,\n"
             "  \"max_active_archive_mutators\": %d,\n"
             "  \"passed\": %s\n"
             "}\n",
             add_compute_ok ? "true" : "false", add_render_ok ? "true" : "false", baseline_ok ? "true" : "false",
             (unsigned long long)serializations_before_threshold, before_threshold_size,
             (unsigned long long)serializations_after_threshold, archive_size, strict_lookup_ok ? "true" : "false",
             max_active, passed ? "true" : "false");

    bool wrote = WriteFile(output, buffer);
    [compute_desc release];
    [render_desc release];
    [ctx.archive release];
    [cs release];
    [vs release];
    [ps release];
    [library release];
    [pool drain];
    return wrote && passed ? 0 : 6;
}

int main(int argc, char** argv) {
    const char* output = NULL;
    const char* archive_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--archive") && i + 1 < argc)
            archive_path = argv[++i];
    }
    if (!output || !archive_path)
        return 1;
    return RunProbe(output, archive_path);
}
