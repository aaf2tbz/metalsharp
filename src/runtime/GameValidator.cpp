#include <metalsharp/GameValidator.h>
#include <metalsharp/ImportReporter.h>
#include <metalsharp/Logger.h>
#include <sstream>
#include <cstring>

namespace metalsharp {

GameValidator& GameValidator::instance() {
    static GameValidator inst;
    return inst;
}

void GameValidator::init(const std::string& dataDir) {
    CompatDatabase::instance().init(dataDir + "/compat_db.json");
    m_initialized = true;
    MS_INFO("GameValidator initialized");
}

void GameValidator::shutdown() {
    if (m_initialized) {
        CompatDatabase::instance().shutdown();
    }
    m_initialized = false;
}

std::string GameValidator::generateGameId(const std::string& exePath) const {
    size_t slash = exePath.rfind('/');
    std::string filename = (slash != std::string::npos) ? exePath.substr(slash + 1) : exePath;

    for (auto& c : filename) {
        if (c == '.' || c == ' ' || c == '\\') c = '_';
    }
    return filename;
}

CompatStatus GameValidator::estimateStatus(const ValidationResult& result) const {
    if (result.drmResults.size() > 0) {
        auto& detector = DRMDetector::instance();
        if (detector.hasKernelAntiCheat(result.drmResults)) {
            return CompatStatus::Broken;
        }
    }

    if (result.missingImports.empty()) {
        return CompatStatus::Untested;
    }

    size_t criticalMissing = 0;
    for (const auto& imp : result.missingImports) {
        if (imp.dll.find("d3d") != std::string::npos ||
            imp.dll.find("D3D") != std::string::npos ||
            imp.dll.find("dxgi") != std::string::npos ||
            imp.dll.find("DXGI") != std::string::npos ||
            imp.dll.find("kernel32") != std::string::npos ||
            imp.dll.find("user32") != std::string::npos) {
            criticalMissing++;
        }
    }

    if (criticalMissing > 5) return CompatStatus::Broken;
    if (criticalMissing > 0) return CompatStatus::Bronze;
    if (result.missingImports.size() > 20) return CompatStatus::Silver;
    return CompatStatus::Gold;
}

ValidationResult GameValidator::validate(const std::string& exePath, const std::string& gameName) {
    ValidationResult result;
    result.gameId = generateGameId(exePath);

    MS_INFO("GameValidator: validating %s (id: %s)", exePath.c_str(), result.gameId.c_str());

    auto& detector = DRMDetector::instance();
    result.drmResults = detector.scanFile(exePath);

    std::ostringstream report;
    report << "=== Validation Report: " << (gameName.empty() ? exePath : gameName) << " ===\n\n";

    report << "--- DRM / Anti-cheat Detection ---\n";
    if (result.drmResults.empty()) {
        report << "No signatures scanned (file not found or empty)\n";
    } else {
        bool anyDetected = false;
        for (const auto& d : result.drmResults) {
            if (d.detected) {
                report << "  DETECTED: " << d.name << " [" << d.type
                       << "] (confidence: " << (int)(d.confidence * 100) << "%)\n";
                anyDetected = true;
            }
        }
        if (!anyDetected) {
            report << "No DRM or anti-cheat detected\n";
        }
    }

    report << "\n--- Import Analysis ---\n";
    auto& importReporter = ImportReporter::instance();
    auto missing = importReporter.missingImports();
    auto missingDlls = importReporter.missingDlls();

    for (const auto& r : missing) {
        MissingImport mi;
        mi.dll = r.dllName;
        mi.function = r.functionName;
        mi.isOrdinal = r.isOrdinal;
        mi.ordinal = r.ordinal;
        result.missingImports.push_back(mi);
    }

    report << "Total imports:  " << importReporter.totalCount() << "\n";
    report << "Resolved:       " << importReporter.resolvedCount() << "\n";
    report << "Missing:        " << importReporter.missingCount() << "\n";

    if (!missingDlls.empty()) {
        report << "\nDLLs with missing imports:\n";
        for (const auto& dll : missingDlls) {
            report << "  " << dll << "\n";
        }
    }

    result.canLaunch = !detector.hasKernelAntiCheat(result.drmResults);
    result.suggestedStatus = estimateStatus(result);

    report << "\n--- Assessment ---\n";
    report << "Can launch:     " << (result.canLaunch ? "Yes" : "No (kernel anti-cheat)") << "\n";
    report << "Suggested status: " << CompatDatabase::statusToString(result.suggestedStatus) << "\n";

    result.report = report.str();
    MS_INFO("GameValidator: validation complete — %s", CompatDatabase::statusToString(result.suggestedStatus));

    return result;
}

ValidationResult GameValidator::quickCheck(const std::string& exePath) {
    ValidationResult result;
    result.gameId = generateGameId(exePath);

    auto& detector = DRMDetector::instance();
    result.drmResults = detector.scanFile(exePath);
    result.canLaunch = detector.isCompatible(result.drmResults);
    result.suggestedStatus = detector.hasKernelAntiCheat(result.drmResults)
        ? CompatStatus::Broken : CompatStatus::Untested;

    return result;
}

bool GameValidator::saveResult(const ValidationResult& result) {
    GameEntry entry;
    entry.gameId = result.gameId;
    entry.status = result.suggestedStatus;
    entry.missingImports = result.missingImports;

    for (const auto& d : result.drmResults) {
        if (d.detected) {
            if (d.type == "Anti-cheat") {
                entry.antiCheat += (entry.antiCheat.empty() ? "" : ", ") + d.name;
            } else if (d.type == "DRM") {
                entry.drm += (entry.drm.empty() ? "" : ", ") + d.name;
            }
        }
    }

    return CompatDatabase::instance().addOrUpdate(entry);
}

GameEntry* GameValidator::findGame(const std::string& gameId) {
    return CompatDatabase::instance().find(gameId);
}

std::string GameValidator::generateFullReport(const std::string& gameId) const {
    return CompatDatabase::instance().generateReport(gameId);
}

}
