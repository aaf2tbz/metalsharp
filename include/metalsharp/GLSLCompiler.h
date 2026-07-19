#pragma once
#include <cstdint>
#include <metalsharp/ShaderStage.h>
#include <string>
#include <vector>

namespace metalsharp {

struct GLSLVersion;

class GLSLCompiler {
  public:
    static bool initialize();
    static void shutdown();
    static bool isAvailable();

    static bool compileToSPIRV(const char* source, ShaderStage stage, const GLSLVersion& version,
                               std::vector<uint32_t>& spirvOut, std::string& errorLog);

  private:
    static bool s_initialized;
};

} // namespace metalsharp