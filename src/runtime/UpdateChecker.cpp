#include "metalsharp/UpdateChecker.h"
#include <sstream>
#include <algorithm>

namespace metalsharp {

bool Version::operator>(const Version& o) const {
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    return patch > o.patch;
}

bool Version::operator==(const Version& o) const {
    return major == o.major && minor == o.minor && patch == o.patch;
}

bool Version::operator>=(const Version& o) const {
    return *this > o || *this == o;
}

std::string Version::toString() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) s += "-" + prerelease;
    return s;
}

Version Version::parse(const std::string& s) {
    Version v;
    std::string input = s;
    if (input.size() > 0 && input[0] == 'v') input = input.substr(1);

    size_t dashPos = input.find('-');
    std::string versionPart = (dashPos != std::string::npos) ? input.substr(0, dashPos) : input;
    if (dashPos != std::string::npos) v.prerelease = input.substr(dashPos + 1);

    std::istringstream ss(versionPart);
    std::string token;
    int idx = 0;
    while (std::getline(ss, token, '.')) {
        try {
            uint32_t val = static_cast<uint32_t>(std::stoul(token));
            if (idx == 0) v.major = val;
            else if (idx == 1) v.minor = val;
            else if (idx == 2) v.patch = val;
        } catch (...) {}
        idx++;
    }

    return v;
}

Version UpdateChecker::getCurrentVersion() {
    return Version::parse("0.1.0");
}

std::string UpdateChecker::getUserAgent() {
    return "MetalSharp/0.1.0 (macOS)";
}

std::string UpdateChecker::fetchLatestRelease(const std::string& repo) {
    return "";
}

UpdateInfo UpdateChecker::parseGitHubRelease(const std::string& json, const Version& current) {
    UpdateInfo info;
    info.currentVersion = current.toString();

    auto extractJsonString = [](const std::string& j, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        auto pos = j.find(search);
        if (pos == std::string::npos) return "";
        auto colon = j.find(':', pos + search.length());
        if (colon == std::string::npos) return "";
        auto openQuote = j.find('"', colon + 1);
        if (openQuote == std::string::npos) return "";
        auto closeQuote = j.find('"', openQuote + 1);
        if (closeQuote == std::string::npos) return "";
        return j.substr(openQuote + 1, closeQuote - openQuote - 1);
    };

    std::string tag = extractJsonString(json, "tag_name");
    if (!tag.empty()) {
        info.latestVersion = Version::parse(tag);
        info.available = info.latestVersion > current;
    }

    info.downloadUrl = extractJsonString(json, "zipball_url");
    info.releaseNotes = extractJsonString(json, "body");
    info.htmlUrl = extractJsonString(json, "html_url");

    return info;
}

UpdateInfo UpdateChecker::checkForUpdates(const std::string& repo) {
    Version current = getCurrentVersion();
    std::string json = fetchLatestRelease(repo);
    if (json.empty()) {
        UpdateInfo info;
        info.currentVersion = current.toString();
        info.available = false;
        return info;
    }
    return parseGitHubRelease(json, current);
}

}
