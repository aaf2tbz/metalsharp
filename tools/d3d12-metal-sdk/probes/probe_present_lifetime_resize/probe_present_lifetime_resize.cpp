#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct CaseResult {
    std::string name;
    bool pass = false;
    std::string detail;
};

static std::string getenv_string(const char* key) {
    DWORD needed = GetEnvironmentVariableA(key, nullptr, 0);
    if (needed == 0)
        return "";
    std::string value(needed, '\0');
    DWORD written = GetEnvironmentVariableA(key, value.data(), needed);
    if (written == 0)
        return "";
    value.resize(written);
    return value;
}

static std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static std::string unix_to_wine_path(std::string path) {
    if (path.empty())
        return path;
    if (path.size() > 2 && path[1] == ':')
        return path;
    if (path[0] == '/') {
        std::string converted = "Z:";
        for (char c : path)
            converted += (c == '/') ? '\\' : c;
        return converted;
    }
    return path;
}

static std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static std::string extract_function_body(const std::string& text, const std::string& signature) {
    size_t start = text.find(signature);
    if (start == std::string::npos)
        return "";
    size_t brace_start = text.find('{', start);
    if (brace_start == std::string::npos)
        return "";
    int depth = 0;
    for (size_t i = brace_start; i < text.size(); ++i) {
        if (text[i] == '{')
            ++depth;
        if (text[i] == '}') {
            --depth;
            if (depth == 0)
                return text.substr(start, i - start + 1);
        }
    }
    return "";
}

static CaseResult check_live_present_default_ordered(const std::string& swapchain) {
    CaseResult r;
    r.name = "live_present_default_ordered";
    r.pass = contains(swapchain, "static bool LivePresentEnabled()") &&
             contains(swapchain, "std::getenv(\"DXMT_D3D12_LIVE_PRESENT\")") &&
             contains(swapchain, "if (LivePresentEnabled())") &&
             contains(swapchain, "dxmt_queue.WaitCPUFence(present_wait_seq)");
    r.detail = r.pass ? "DXMT_D3D12_LIVE_PRESENT is explicit opt-in and default path waits for producer CPU fence"
                      : "live-present/default wait contract not found";
    return r;
}

static CaseResult check_present_submission_retention(const std::string& swapchain) {
    CaseResult r;
    r.name = "present_submission_retains_backbuffer_textures_drawable";
    r.pass = contains(swapchain, "RetainPresentSubmissionReferences") &&
             contains(swapchain, "refs.RetainResource(resource)") &&
             contains(swapchain, "refs.RetainTexture(src_texture)") &&
             contains(swapchain, "refs.RetainTexture(dst_texture)") &&
             contains(swapchain, "refs.RetainDrawable(drawable)") &&
             contains(swapchain, "refs.RetainTexture(drawable.texture())") &&
             contains(swapchain, "TrackPresentCommandBufferWithReferences") &&
             contains(swapchain, "RetainPresentSubmissionReferences(submission_refs, resource, src_texture,") &&
             contains(swapchain, "TrackPresentCommandBuffer(cmdbuf, std::move(submission_refs))");
    r.detail = r.pass ? "present command buffers retain source backbuffer/resource, source/destination textures, "
                        "drawable, and drawable texture until completion"
                      : "present submission retention contract not found";
    return r;
}

static CaseResult check_present_hot_path_avoids_heavy_retention_temporaries(const std::string& swapchain) {
    CaseResult r;
    r.name = "present_hot_path_avoids_heavy_retention_temporaries";
    std::string present1 = extract_function_body(swapchain, "MTLD3D12SwapChain::Present1(");
    r.pass = !present1.empty() && !contains(present1, "D3D12MetalSubmissionReferences") &&
             !contains(present1, "RetainPresentSubmissionReferences") &&
             !contains(present1, "MakePresentSubmissionReferences") &&
             contains(present1, "TrackPresentCommandBufferWithReferences");
    r.detail =
        r.pass
            ? "Present1 delegates present retention out of the hot path instead of constructing heavyweight "
              "submission-reference temporaries inline"
            : "Present1 still appears to construct heavy retention objects inline or lacks delegated present retention";
    return r;
}

static CaseResult check_single_present_ownership(const std::string& swapchain) {
    CaseResult r;
    r.name = "single_drawable_present_ownership";
    r.pass = contains(swapchain, "if (!native_present_executed") &&
             contains(swapchain, "cmdbuf.presentDrawable(drawable)") &&
             contains(swapchain, "native_present_executed = true") && contains(swapchain, "raw-post-native") &&
             contains(swapchain, "presenter-fallback");
    r.detail = r.pass ? "presenter/raw/native ownership paths are mutually classified and raw presentDrawable is "
                        "skipped after native execution"
                      : "single present ownership contract not found";
    return r;
}

static CaseResult check_resize_cage(const std::string& swapchain) {
    CaseResult r;
    r.name = "resize_drains_present_inflight_before_backbuffer_recreate";
    auto resize_pos = swapchain.find("HRESULT STDMETHODCALLTYPE MTLD3D12SwapChain::ResizeBuffers");
    auto drain_pos = swapchain.find("DrainPresentCommandBuffers(true);", resize_pos);
    auto release_pos = swapchain.find("m_backbuffers[i] = nullptr;", resize_pos);
    r.pass = resize_pos != std::string::npos && drain_pos != std::string::npos && release_pos != std::string::npos &&
             drain_pos < release_pos;
    r.detail =
        r.pass ? "ResizeBuffers drains in-flight present command buffers before releasing/recreating old backbuffers"
               : "resize drain-before-release contract not found";
    return r;
}

static CaseResult check_completion_slots_release_after_done(const std::string& swapchain) {
    CaseResult r;
    r.name = "present_completion_slots_release_refs_after_completion";
    r.pass = contains(swapchain, "WaitForPresentCommandBufferSlot") &&
             contains(swapchain, "slot.cmdbuf.waitUntilCompleted()") &&
             contains(swapchain, "ResetD3D12MetalCompletionSlot(slot)") &&
             contains(swapchain, "TrackPresentCommandBuffer(") && contains(swapchain, "ArmD3D12MetalCompletionSlot(");
    r.detail = r.pass
                   ? "present inflight slots wait/observe completion before Reset clears retained submission references"
                   : "present completion-slot release contract not found";
    return r;
}

static void print_case(const CaseResult& c, bool last) {
    std::printf("    {\n");
    std::printf("      \"name\": \"%s\",\n", json_escape(c.name).c_str());
    std::printf("      \"pass\": %s,\n", c.pass ? "true" : "false");
    std::printf("      \"detail\": \"%s\"\n", json_escape(c.detail).c_str());
    std::printf("    }%s\n", last ? "" : ",");
}

int main() {
    std::string profile = getenv_string("D3D12_METAL_SDK_PROFILE");
    std::string raw_root = getenv_string("M12_SOURCE_ROOT");
    if (raw_root.empty())
        raw_root = ".";
    std::string wine_root = unix_to_wine_path(raw_root);

    std::vector<std::string> candidates = {
        raw_root + "/vendor/dxmt/src/d3d12/d3d12_swapchain.cpp",
        wine_root + "\\vendor\\dxmt\\src\\d3d12\\d3d12_swapchain.cpp",
        ".\\vendor\\dxmt\\src\\d3d12\\d3d12_swapchain.cpp",
        "..\\..\\..\\..\\vendor\\dxmt\\src\\d3d12\\d3d12_swapchain.cpp",
    };
    std::string swapchain_path;
    std::string swapchain;
    for (const auto& candidate : candidates) {
        swapchain = read_file(candidate);
        if (!swapchain.empty()) {
            swapchain_path = candidate;
            break;
        }
    }

    std::vector<CaseResult> cases;
    if (swapchain.empty()) {
        cases.push_back({"source_load", false, "failed to load d3d12_swapchain.cpp from M12_SOURCE_ROOT"});
    } else {
        cases.push_back(check_live_present_default_ordered(swapchain));
        cases.push_back(check_present_submission_retention(swapchain));
        cases.push_back(check_present_hot_path_avoids_heavy_retention_temporaries(swapchain));
        cases.push_back(check_single_present_ownership(swapchain));
        cases.push_back(check_resize_cage(swapchain));
        cases.push_back(check_completion_slots_release_after_done(swapchain));
    }

    bool pass = true;
    for (const auto& c : cases)
        pass = pass && c.pass;

    std::printf("{\n");
    std::printf("  \"schema\": \"metalsharp.d3d12-metal.probe-present-lifetime-resize.v1\",\n");
    std::printf("  \"profile\": \"%s\",\n", json_escape(profile).c_str());
    std::printf("  \"pass\": %s,\n", pass ? "true" : "false");
    std::printf("  \"source\": {\n");
    std::printf("    \"root\": \"%s\",\n", json_escape(raw_root).c_str());
    std::printf("    \"swapchain\": \"%s\"\n", json_escape(swapchain_path).c_str());
    std::printf("  },\n");
    std::printf("  \"coverage\": {\n");
    std::printf("    \"live_present_default_ordered\": true,\n");
    std::printf("    \"present_submission_retention\": true,\n");
    std::printf("    \"present_hot_path_shape\": true,\n");
    std::printf("    \"single_drawable_present_ownership\": true,\n");
    std::printf("    \"resize_recreate_cage\": true,\n");
    std::printf("    \"completion_slot_release_after_completion\": true\n");
    std::printf("  },\n");
    std::printf("  \"cases\": [\n");
    for (size_t i = 0; i < cases.size(); ++i)
        print_case(cases[i], i + 1 == cases.size());
    std::printf("  ]\n");
    std::printf("}\n");
    std::fflush(stdout);
    return pass ? 0 : 1;
}
