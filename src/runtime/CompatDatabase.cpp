#include <metalsharp/CompatDatabase.h>
#include <metalsharp/Logger.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace metalsharp {

static const char* kStatusNames[] = {"Platinum", "Gold", "Silver", "Bronze", "Broken", "Untested"};

CompatDatabase& CompatDatabase::instance() {
    static CompatDatabase inst;
    return inst;
}

CompatStatus CompatDatabase::statusFromString(const std::string& s) {
    if (s == "Platinum") return CompatStatus::Platinum;
    if (s == "Gold") return CompatStatus::Gold;
    if (s == "Silver") return CompatStatus::Silver;
    if (s == "Bronze") return CompatStatus::Bronze;
    if (s == "Broken") return CompatStatus::Broken;
    return CompatStatus::Untested;
}

const char* CompatDatabase::statusToString(CompatStatus s) {
    return kStatusNames[static_cast<int>(s)];
}

std::string GameEntry::statusStr() const {
    return CompatDatabase::statusToString(status);
}

bool CompatDatabase::init(const std::string& dbPath) {
    m_dbPath = dbPath;
    m_initialized = true;
    loadFromDisk();
    MS_INFO("CompatDatabase initialized: %zu entries from %s", m_entries.size(), m_dbPath.c_str());
    return true;
}

void CompatDatabase::shutdown() {
    if (m_initialized) {
        saveToDisk();
    }
    m_entries.clear();
    m_initialized = false;
}

bool CompatDatabase::addOrUpdate(const GameEntry& entry) {
    m_entries[entry.gameId] = entry;
    return true;
}

bool CompatDatabase::remove(const std::string& gameId) {
    return m_entries.erase(gameId) > 0;
}

GameEntry* CompatDatabase::find(const std::string& gameId) {
    auto it = m_entries.find(gameId);
    return it != m_entries.end() ? &it->second : nullptr;
}

const GameEntry* CompatDatabase::find(const std::string& gameId) const {
    auto it = m_entries.find(gameId);
    return it != m_entries.end() ? &it->second : nullptr;
}

std::vector<GameEntry> CompatDatabase::queryByStatus(CompatStatus status) const {
    std::vector<GameEntry> result;
    for (const auto& [id, entry] : m_entries) {
        if (entry.status == status) result.push_back(entry);
    }
    return result;
}

std::vector<GameEntry> CompatDatabase::queryAll() const {
    std::vector<GameEntry> result;
    for (const auto& [id, entry] : m_entries) {
        result.push_back(entry);
    }
    return result;
}

size_t CompatDatabase::count() const {
    return m_entries.size();
}

size_t CompatDatabase::countByStatus(CompatStatus status) const {
    size_t c = 0;
    for (const auto& [id, entry] : m_entries) {
        if (entry.status == status) c++;
    }
    return c;
}

void CompatDatabase::addMissingImport(const std::string& gameId, const MissingImport& imp) {
    auto* entry = find(gameId);
    if (!entry) {
        GameEntry ge;
        ge.gameId = gameId;
        ge.status = CompatStatus::Untested;
        m_entries[gameId] = ge;
        entry = &m_entries[gameId];
    }

    for (const auto& existing : entry->missingImports) {
        if (existing.dll == imp.dll && existing.function == imp.function) return;
    }
    entry->missingImports.push_back(imp);
}

void CompatDatabase::addCrashRecord(const std::string& gameId, const CrashRecord& crash) {
    auto* entry = find(gameId);
    if (!entry) {
        GameEntry ge;
        ge.gameId = gameId;
        ge.status = CompatStatus::Broken;
        m_entries[gameId] = ge;
        entry = &m_entries[gameId];
    }
    entry->crashes.push_back(crash);
    if (entry->status == CompatStatus::Untested) {
        entry->status = CompatStatus::Broken;
    }
}

std::string CompatDatabase::generateReport(const std::string& gameId) const {
    const auto* entry = find(gameId);
    if (!entry) return "Game not found: " + gameId + "\n";

    std::ostringstream ss;
    ss << "=== Compatibility Report: " << entry->name << " ===\n";
    ss << "Game ID:    " << entry->gameId << "\n";
    ss << "EXE:        " << entry->exePath << "\n";
    ss << "Platform:   " << entry->platform << "\n";
    ss << "D3D:        " << entry->d3dVersion << "\n";
    ss << "Status:     " << entry->statusStr() << "\n";
    ss << "Anti-cheat: " << (entry->antiCheat.empty() ? "None detected" : entry->antiCheat) << "\n";
    ss << "DRM:        " << (entry->drm.empty() ? "None detected" : entry->drm) << "\n";
    ss << "Avg FPS:    " << entry->avgFPS << "\n";
    ss << "Tester:     " << (entry->tester.empty() ? "unknown" : entry->tester) << "\n";
    ss << "Last tested:" << (entry->lastTested > 0 ? " recently" : " never") << "\n";

    if (!entry->missingImports.empty()) {
        ss << "\n--- Missing Imports (" << entry->missingImports.size() << ") ---\n";
        for (const auto& imp : entry->missingImports) {
            if (imp.isOrdinal) {
                ss << "  " << imp.dll << "!ordinal_" << imp.ordinal << "\n";
            } else {
                ss << "  " << imp.dll << "!" << imp.function << "\n";
            }
        }
    }

    if (!entry->crashes.empty()) {
        ss << "\n--- Crash History (" << entry->crashes.size() << ") ---\n";
        for (const auto& crash : entry->crashes) {
            ss << "  Signal " << crash.signal << " at 0x" << std::hex << crash.faultAddr << std::dec;
            ss << " (RIP 0x" << std::hex << crash.rip << std::dec << ")";
            if (!crash.crashRVA.empty()) ss << " RVA " << crash.crashRVA;
            ss << "\n";
        }
    }

    if (!entry->workarounds.empty()) {
        ss << "\n--- Workarounds ---\n" << entry->workarounds << "\n";
    }

    if (!entry->notes.empty()) {
        ss << "\n--- Notes ---\n" << entry->notes << "\n";
    }

    return ss.str();
}

static std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string readQuotedString(const std::string& json, size_t& pos) {
    std::string result;
    if (pos >= json.size() || json[pos] != '"') return result;
    pos++;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            if (json[pos] == 'n') result += '\n';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    if (pos < json.size()) pos++;
    return result;
}

static void skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;
}

static std::string readValue(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (pos >= json.size()) return "";
    if (json[pos] == '"') return readQuotedString(json, pos);
    size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
        pos++;
    return json.substr(start, pos - start);
}

static uint64_t readUint(const std::string& json, size_t& pos) {
    std::string val = readValue(json, pos);
    try { return std::stoull(val); } catch (...) { return 0; }
}

static uint32_t readUint32(const std::string& json, size_t& pos) {
    std::string val = readValue(json, pos);
    try { return static_cast<uint32_t>(std::stoul(val)); } catch (...) { return 0; }
}

static int readInt(const std::string& json, size_t& pos) {
    std::string val = readValue(json, pos);
    try { return std::stoi(val); } catch (...) { return 0; }
}

static std::string findField(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') pos++;
    skipWhitespace(json, pos);
    return readValue(json, pos);
}

static std::vector<std::string> findArray(const std::string& json, const std::string& key) {
    std::vector<std::string> items;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return items;
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') pos++;
    skipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '[') return items;
    pos++;
    while (pos < json.size() && json[pos] != ']') {
        skipWhitespace(json, pos);
        if (json[pos] == '"') {
            items.push_back(readQuotedString(json, pos));
        } else {
            pos++;
        }
    }
    return items;
}

bool CompatDatabase::saveToDisk() {
    if (m_dbPath.empty()) return false;

    std::ofstream file(m_dbPath);
    if (!file.is_open()) return false;

    file << "{\n  \"version\": 1,\n  \"games\": [\n";
    bool first = true;
    for (const auto& [id, e] : m_entries) {
        if (!first) file << ",\n";
        first = false;

        file << "    {\n";
        file << "      \"gameId\": \"" << escapeJson(e.gameId) << "\",\n";
        file << "      \"name\": \"" << escapeJson(e.name) << "\",\n";
        file << "      \"exePath\": \"" << escapeJson(e.exePath) << "\",\n";
        file << "      \"platform\": \"" << escapeJson(e.platform) << "\",\n";
        file << "      \"status\": \"" << e.statusStr() << "\",\n";
        file << "      \"d3dVersion\": " << e.d3dVersion << ",\n";
        file << "      \"antiCheat\": \"" << escapeJson(e.antiCheat) << "\",\n";
        file << "      \"drm\": \"" << escapeJson(e.drm) << "\",\n";
        file << "      \"notes\": \"" << escapeJson(e.notes) << "\",\n";
        file << "      \"workarounds\": \"" << escapeJson(e.workarounds) << "\",\n";
        file << "      \"lastTested\": " << e.lastTested << ",\n";
        file << "      \"avgFPS\": " << e.avgFPS << ",\n";
        file << "      \"tester\": \"" << escapeJson(e.tester) << "\",\n";

        file << "      \"missingImports\": [";
        for (size_t i = 0; i < e.missingImports.size(); i++) {
            const auto& imp = e.missingImports[i];
            if (i > 0) file << ",";
            file << "\n        {\"dll\":\"" << escapeJson(imp.dll) << "\","
                 << "\"fn\":\"" << escapeJson(imp.function) << "\","
                 << "\"ord\":" << (imp.isOrdinal ? "true" : "false") << ","
                 << "\"n\":" << imp.ordinal << "}";
        }
        if (!e.missingImports.empty()) file << "\n      ";
        file << "],\n";

        file << "      \"crashes\": [";
        for (size_t i = 0; i < e.crashes.size(); i++) {
            const auto& c = e.crashes[i];
            if (i > 0) file << ",";
            file << "\n        {\"ts\":" << c.timestamp << ",\"sig\":" << c.signal
                 << ",\"fa\":" << c.faultAddr << ",\"rip\":" << c.rip
                 << ",\"rva\":\"" << escapeJson(c.crashRVA) << "\"}";
        }
        if (!e.crashes.empty()) file << "\n      ";
        file << "]\n";

        file << "    }";
    }
    file << "\n  ]\n}\n";

    MS_INFO("CompatDatabase: saved %zu entries to %s", m_entries.size(), m_dbPath.c_str());
    return true;
}

bool CompatDatabase::loadFromDisk() {
    if (m_dbPath.empty()) return false;

    std::ifstream file(m_dbPath);
    if (!file.is_open()) return false;

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    if (json.empty()) return false;

    size_t gamesPos = json.find("\"games\"");
    if (gamesPos == std::string::npos) return true;

    size_t pos = json.find('[', gamesPos);
    if (pos == std::string::npos) return true;
    pos++;

    while (pos < json.size()) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        size_t objEnd = json.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string obj = json.substr(objStart, objEnd - objStart + 1);

        GameEntry entry;
        entry.gameId = findField(obj, "gameId");
        entry.name = findField(obj, "name");
        entry.exePath = findField(obj, "exePath");
        entry.platform = findField(obj, "platform");
        entry.status = statusFromString(findField(obj, "status"));
        entry.d3dVersion = 0;
        entry.lastTested = 0;
        entry.avgFPS = 0;

        {
            size_t p = 0;
            std::string dv = findField(obj, "d3dVersion");
            if (!dv.empty()) try { entry.d3dVersion = std::stoi(dv); } catch (...) {}
        }

        entry.antiCheat = findField(obj, "antiCheat");
        entry.drm = findField(obj, "drm");
        entry.notes = findField(obj, "notes");
        entry.workarounds = findField(obj, "workarounds");
        entry.tester = findField(obj, "tester");

        {
            std::string lt = findField(obj, "lastTested");
            if (!lt.empty()) try { entry.lastTested = std::stoull(lt); } catch (...) {}
        }
        {
            std::string fps = findField(obj, "avgFPS");
            if (!fps.empty()) try { entry.avgFPS = std::stoi(fps); } catch (...) {}
        }

        size_t impPos = obj.find("\"missingImports\"");
        if (impPos != std::string::npos) {
            size_t arrStart = obj.find('[', impPos);
            size_t arrEnd = obj.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string impArr = obj.substr(arrStart, arrEnd - arrStart + 1);
                size_t ip = 0;
                while (ip < impArr.size()) {
                    size_t impObj = impArr.find('{', ip);
                    if (impObj == std::string::npos) break;
                    size_t impEnd = impArr.find('}', impObj);
                    if (impEnd == std::string::npos) break;

                    std::string impData = impArr.substr(impObj, impEnd - impObj + 1);
                    MissingImport mi;
                    mi.dll = findField(impData, "dll");
                    mi.function = findField(impData, "fn");
                    std::string ord = findField(impData, "ord");
                    mi.isOrdinal = (ord == "true");
                    std::string n = findField(impData, "n");
                    if (!n.empty()) try { mi.ordinal = std::stoi(n); } catch (...) {}
                    entry.missingImports.push_back(mi);

                    ip = impEnd + 1;
                }
            }
        }

        if (!entry.gameId.empty()) {
            m_entries[entry.gameId] = entry;
        }

        pos = objEnd + 1;
    }

    MS_INFO("CompatDatabase: loaded %zu entries from disk", m_entries.size());
    return true;
}

}
