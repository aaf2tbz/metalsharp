#pragma once

#include <string>
#include <cstdint>

namespace metalsharp {

struct Version {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::string prerelease;

    bool operator>(const Version& o) const;
    bool operator==(const Version& o) const;
    bool operator>=(const Version& o) const;
    std::string toString() const;
    static Version parse(const std::string& s);
};

struct UpdateInfo {
    bool available = false;
    Version latestVersion;
    std::string currentVersion;
    std::string downloadUrl;
    std::string releaseNotes;
    std::string htmlUrl;
};

class UpdateChecker {
public:
    static UpdateInfo checkForUpdates(const std::string& repo = "aaf2tbz/metalsharp");
    static Version getCurrentVersion();
    static std::string getUserAgent();
    static UpdateInfo parseGitHubRelease(const std::string& json, const Version& current);

private:
    static std::string fetchLatestRelease(const std::string& repo);
};

}
