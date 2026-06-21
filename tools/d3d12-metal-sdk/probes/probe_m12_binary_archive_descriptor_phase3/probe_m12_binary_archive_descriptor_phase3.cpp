#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using obj_handle_t = unsigned long long;

static constexpr obj_handle_t NULL_OBJECT_HANDLE = 0;

struct WMTConstMemoryPointer {
    const void* ptr = nullptr;

    void set(const void* p) { ptr = p; }
};

struct WMTComputePipelineInfo {
    obj_handle_t compute_function = NULL_OBJECT_HANDLE;
    WMTConstMemoryPointer binary_archives_for_lookup;
    obj_handle_t binary_archive_for_serialization = NULL_OBJECT_HANDLE;
    unsigned char num_binary_archives_for_lookup = 0;
    bool fail_on_binary_archive_miss = false;
};

struct WMTRenderPipelineInfo {
    obj_handle_t vertex_function = NULL_OBJECT_HANDLE;
    obj_handle_t fragment_function = NULL_OBJECT_HANDLE;
    obj_handle_t binary_archive_for_serialization = NULL_OBJECT_HANDLE;
    WMTConstMemoryPointer binary_archives_for_lookup;
    unsigned char num_binary_archives_for_lookup = 0;
    bool fail_on_binary_archive_miss = false;
};

struct ProbeArchiveContext {
    obj_handle_t archive = NULL_OBJECT_HANDLE;
    bool enabled = false;
    bool allow_lookup = false;
    bool allow_population = false;
};

struct M12BinaryArchiveCompilePayload {
    obj_handle_t heap_archive_handles[1] = {NULL_OBJECT_HANDLE};
};

template <typename PipelineInfo>
static void attachArchiveInfo(PipelineInfo& info, ProbeArchiveContext& context,
                              M12BinaryArchiveCompilePayload& payload) {
    info.binary_archive_for_serialization = NULL_OBJECT_HANDLE;
    info.binary_archives_for_lookup.set(nullptr);
    info.num_binary_archives_for_lookup = 0;
    info.fail_on_binary_archive_miss = false;
    payload.heap_archive_handles[0] = NULL_OBJECT_HANDLE;

    if (!context.enabled || !context.archive)
        return;

    payload.heap_archive_handles[0] = context.archive;
    if (context.allow_population)
        info.binary_archive_for_serialization = context.archive;
    if (context.allow_lookup) {
        info.binary_archives_for_lookup.set(payload.heap_archive_handles);
        info.num_binary_archives_for_lookup = 1;
    }
}

struct ComputeCompileRequest {
    M12BinaryArchiveCompilePayload payload;
    WMTComputePipelineInfo info;
};

struct RenderCompileRequest {
    M12BinaryArchiveCompilePayload payload;
    WMTRenderPipelineInfo info;
};

static std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

static bool writeFile(const char* path, const std::string& text) {
    FILE* f = std::fopen(path, "w");
    if (!f)
        return false;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return true;
}

static bool isLookupAllowedCase(const char* name) {
    return std::strcmp(name, "lookup-allowed") == 0;
}

static ProbeArchiveContext contextForCase(const char* name) {
    if (std::strcmp(name, "disabled") == 0)
        return {};
    if (std::strcmp(name, "lookup-bypassed") == 0)
        return {0x123456789abcdef0ULL, true, false, false};
    if (std::strcmp(name, "lookup-allowed") == 0)
        return {0x123456789abcdef0ULL, true, true, false};
    if (std::strcmp(name, "population-enabled") == 0)
        return {0x123456789abcdef0ULL, true, false, true};
    if (std::strcmp(name, "circuit-breaker") == 0)
        return {0x123456789abcdef0ULL, true, false, false};
    return {};
}

template <typename Request>
static bool validateRequest(const Request& request, const ProbeArchiveContext& context, bool lookup_allowed) {
    const auto& info = request.info;
    const obj_handle_t* lookup = static_cast<const obj_handle_t*>(info.binary_archives_for_lookup.ptr);
    if (info.fail_on_binary_archive_miss)
        return false;
    if (!context.enabled || !context.archive) {
        return info.binary_archive_for_serialization == NULL_OBJECT_HANDLE && lookup == nullptr &&
               info.num_binary_archives_for_lookup == 0 &&
               request.payload.heap_archive_handles[0] == NULL_OBJECT_HANDLE;
    }
    const obj_handle_t expected_serialization = context.allow_population ? context.archive : NULL_OBJECT_HANDLE;
    if (info.binary_archive_for_serialization != expected_serialization)
        return false;
    if (request.payload.heap_archive_handles[0] != context.archive)
        return false;
    if (lookup_allowed) {
        return lookup == request.payload.heap_archive_handles && info.num_binary_archives_for_lookup == 1 &&
               lookup[0] == context.archive;
    }
    return lookup == nullptr && info.num_binary_archives_for_lookup == 0;
}

static int runCase(const char* case_name, const char* output) {
    ProbeArchiveContext context = contextForCase(case_name);
    const bool lookup_allowed = isLookupAllowedCase(case_name);

    auto compute = std::make_unique<ComputeCompileRequest>();
    auto render = std::make_unique<RenderCompileRequest>();
    compute->info.compute_function = 0x1111;
    render->info.vertex_function = 0x2222;
    render->info.fragment_function = 0x3333;

    attachArchiveInfo(compute->info, context, compute->payload);
    attachArchiveInfo(render->info, context, render->payload);

    const auto* compute_lookup = static_cast<const obj_handle_t*>(compute->info.binary_archives_for_lookup.ptr);
    const auto* render_lookup = static_cast<const obj_handle_t*>(render->info.binary_archives_for_lookup.ptr);
    const bool compute_ok = validateRequest(*compute, context, lookup_allowed);
    const bool render_ok = validateRequest(*render, context, lookup_allowed);
    const bool heap_payload_storage =
        compute_lookup == nullptr || compute_lookup == compute->payload.heap_archive_handles;
    const bool heap_render_payload_storage =
        render_lookup == nullptr || render_lookup == render->payload.heap_archive_handles;
    const bool passed = compute_ok && render_ok && heap_payload_storage && heap_render_payload_storage;

    char buffer[4096];
    std::snprintf(buffer, sizeof(buffer),
                  "{\n"
                  "  \"case\": \"%s\",\n"
                  "  \"enabled\": %s,\n"
                  "  \"allow_lookup\": %s,\n"
                  "  \"allow_population\": %s,\n"
                  "  \"compute_serialization_set\": %s,\n"
                  "  \"render_serialization_set\": %s,\n"
                  "  \"compute_lookup_count\": %u,\n"
                  "  \"render_lookup_count\": %u,\n"
                  "  \"compute_lookup_heap_payload\": %s,\n"
                  "  \"render_lookup_heap_payload\": %s,\n"
                  "  \"compute_fail_on_miss\": %s,\n"
                  "  \"render_fail_on_miss\": %s,\n"
                  "  \"compute_valid\": %s,\n"
                  "  \"render_valid\": %s,\n"
                  "  \"passed\": %s\n"
                  "}\n",
                  case_name, jsonBool(context.enabled).c_str(), jsonBool(context.allow_lookup).c_str(),
                  jsonBool(context.allow_population).c_str(),
                  jsonBool(compute->info.binary_archive_for_serialization != NULL_OBJECT_HANDLE).c_str(),
                  jsonBool(render->info.binary_archive_for_serialization != NULL_OBJECT_HANDLE).c_str(),
                  (unsigned)compute->info.num_binary_archives_for_lookup,
                  (unsigned)render->info.num_binary_archives_for_lookup, jsonBool(heap_payload_storage).c_str(),
                  jsonBool(heap_render_payload_storage).c_str(),
                  jsonBool(compute->info.fail_on_binary_archive_miss).c_str(),
                  jsonBool(render->info.fail_on_binary_archive_miss).c_str(), jsonBool(compute_ok).c_str(),
                  jsonBool(render_ok).c_str(), jsonBool(passed).c_str());

    if (!writeFile(output, buffer))
        return 3;
    return passed ? 0 : 2;
}

int main(int argc, char** argv) {
    const char* case_name = nullptr;
    const char* output = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) {
            case_name = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        }
    }
    if (!case_name || !output)
        return 1;
    return runCase(case_name, output);
}
