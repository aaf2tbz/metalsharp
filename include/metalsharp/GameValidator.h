/// @file GameValidator.h
/// @brief Pre-launch game validation combining DRM detection, import analysis, and compat DB.
///
/// Orchestrates DRMDetector scanning, ImportReporter analysis, and CompatDatabase lookup
/// to produce a ValidationResult with a suggested compatibility tier and full diagnostic
/// report. Quick-check mode does a fast DRM-only scan; full validate runs all checks and
/// persists the result. The launcher uses this to warn about unsupported kernel-only
/// boundaries, protected runtime proof requirements, or missing DLL imports before launch.

#pragma once

#include <cstdint>
#include <metalsharp/CompatDatabase.h>
#include <metalsharp/DRMDetector.h>
#include <metalsharp/Platform.h>
#include <string>

namespace metalsharp {

struct ValidationResult {
    std::string gameId;
    CompatStatus suggestedStatus;
    std::vector<DRMDetection> drmResults;
    std::vector<MissingImport> missingImports;
    std::string report;
    bool canLaunch;
};

class GameValidator {
  public:
    static GameValidator& instance();

    void init(const std::string& dataDir);
    void shutdown();

    ValidationResult validate(const std::string& exePath, const std::string& gameName = "");
    ValidationResult quickCheck(const std::string& exePath);

    bool saveResult(const ValidationResult& result);
    GameEntry* findGame(const std::string& gameId);

    std::string generateFullReport(const std::string& gameId) const;

  private:
    GameValidator() = default;

    CompatStatus estimateStatus(const ValidationResult& result) const;
    std::string generateGameId(const std::string& exePath) const;

    bool m_initialized = false;
};

} // namespace metalsharp
