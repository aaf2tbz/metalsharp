/// @file ImportReporter.h
/// @brief Tracker for resolved and missing DLL imports during PE loading.
///
/// Records every import the PE loader attempts to resolve — both by name and by ordinal —
/// tracking whether each was satisfied by a shim or left unresolved. Aggregates results
/// per-module and provides summary generation showing which DLLs have missing functions.
/// Import reports feed into GameValidator's compatibility assessment and CrashReporter's
/// diagnostic bundles.

#pragma once

#include <metalsharp/Platform.h>
#include <string>
#include <vector>
#include <cstdint>

namespace metalsharp {

struct ImportReport {
    std::string dllName;
    std::string functionName;
    bool isOrdinal = false;
    uint16_t ordinal = 0;
    bool resolved = false;
};

class ImportReporter {
public:
    static ImportReporter& instance();

    void beginModule(const std::string& modulePath);
    void recordImport(const std::string& dll, const std::string& function, bool resolved);
    void recordOrdinalImport(const std::string& dll, uint16_t ordinal, bool resolved);
    void endModule();

    const std::vector<ImportReport>& allImports() const { return m_reports; }
    std::vector<ImportReport> missingImports() const;
    std::vector<ImportReport> resolvedImports() const;
    std::vector<std::string> missingDlls() const;

    size_t totalCount() const { return m_reports.size(); }
    size_t resolvedCount() const;
    size_t missingCount() const;

    std::string generateSummary() const;
    void clear();

private:
    ImportReporter() = default;

    std::vector<ImportReport> m_reports;
    std::string m_currentModule;
};

}
