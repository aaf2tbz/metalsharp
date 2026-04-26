#include <metalsharp/Logger.h>
#include <cstring>
#include <ctime>

namespace metalsharp {

LogLevel Logger::s_level = LogLevel::Info;
FILE* Logger::s_file = nullptr;

void Logger::init(const std::string& logPath) {
    if (!logPath.empty()) {
        s_file = fopen(logPath.c_str(), "a");
    }
    if (!s_file) s_file = stderr;
}

void Logger::shutdown() {
    if (s_file && s_file != stderr) {
        fclose(s_file);
        s_file = nullptr;
    }
}

void Logger::setLevel(LogLevel level) { s_level = level; }

void Logger::log(LogLevel level, const char* fmt, ...) {
    if (level < s_level) return;

    const char* prefix = "INFO";
    switch (level) {
        case LogLevel::Trace: prefix = "TRACE"; break;
        case LogLevel::Info:  prefix = "INFO "; break;
        case LogLevel::Warn:  prefix = "WARN "; break;
        case LogLevel::Error: prefix = "ERROR"; break;
    }

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);

    va_list args;
    va_start(args, fmt);

    if (s_file) {
        fprintf(s_file, "[%s] [%s] ", timebuf, prefix);
        vfprintf(s_file, fmt, args);
        fprintf(s_file, "\n");
        fflush(s_file);
    }

    va_end(args);
}

}
