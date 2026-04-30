#include <metalsharp/ImportReporter.h>
#include <metalsharp/Logger.h>
#include <sstream>
#include <algorithm>

namespace metalsharp {

ImportReporter& ImportReporter::instance() {
    static ImportReporter inst;
    return inst;
}

void ImportReporter::beginModule(const std::string& modulePath) {
    m_currentModule = modulePath;
}

void ImportReporter::recordImport(const std::string& dll, const std::string& function, bool resolved) {
    ImportReport report;
    report.dllName = dll;
    report.functionName = function;
    report.isOrdinal = false;
    report.resolved = resolved;
    m_reports.push_back(report);
}

void ImportReporter::recordOrdinalImport(const std::string& dll, uint16_t ordinal, bool resolved) {
    ImportReport report;
    report.dllName = dll;
    report.isOrdinal = true;
    report.ordinal = ordinal;
    report.resolved = resolved;
    m_reports.push_back(report);
}

void ImportReporter::endModule() {
    m_currentModule.clear();
}

std::vector<ImportReport> ImportReporter::missingImports() const {
    std::vector<ImportReport> result;
    for (const auto& r : m_reports) {
        if (!r.resolved) result.push_back(r);
    }
    return result;
}

std::vector<ImportReport> ImportReporter::resolvedImports() const {
    std::vector<ImportReport> result;
    for (const auto& r : m_reports) {
        if (r.resolved) result.push_back(r);
    }
    return result;
}

std::vector<std::string> ImportReporter::missingDlls() const {
    std::vector<std::string> dlls;
    for (const auto& r : m_reports) {
        if (!r.resolved) {
            if (std::find(dlls.begin(), dlls.end(), r.dllName) == dlls.end()) {
                dlls.push_back(r.dllName);
            }
        }
    }
    return dlls;
}

size_t ImportReporter::resolvedCount() const {
    size_t c = 0;
    for (const auto& r : m_reports) {
        if (r.resolved) c++;
    }
    return c;
}

size_t ImportReporter::missingCount() const {
    size_t c = 0;
    for (const auto& r : m_reports) {
        if (!r.resolved) c++;
    }
    return c;
}

std::string ImportReporter::generateSummary() const {
    std::ostringstream ss;
    ss << "=== Import Resolution Summary ===\n";
    ss << "Total:   " << totalCount() << "\n";
    ss << "Resolved:" << resolvedCount() << "\n";
    ss << "Missing: " << missingCount() << "\n";

    auto missing = missingImports();
    if (!missing.empty()) {
        ss << "\n--- Missing Imports ---\n";
        for (const auto& r : missing) {
            if (r.isOrdinal) {
                ss << "  " << r.dllName << "!ordinal_" << r.ordinal << "\n";
            } else {
                ss << "  " << r.dllName << "!" << r.functionName << "\n";
            }
        }
    }

    auto dlls = missingDlls();
    if (!dlls.empty()) {
        ss << "\n--- DLLs with Missing Imports ---\n";
        for (const auto& dll : dlls) {
            size_t count = 0;
            for (const auto& r : missing) {
                if (r.dllName == dll) count++;
            }
            ss << "  " << dll << " (" << count << " missing)\n";
        }
    }

    return ss.str();
}

void ImportReporter::clear() {
    m_reports.clear();
    m_currentModule.clear();
}

}
