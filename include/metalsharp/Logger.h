#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>

namespace metalsharp {

enum class LogLevel { Trace, Info, Warn, Error };

class Logger {
public:
    static void init(const std::string& logPath);
    static void shutdown();

    static void log(LogLevel level, const char* fmt, ...);
    static void setLevel(LogLevel level);

private:
    static LogLevel s_level;
    static FILE* s_file;
};

}

#define MS_TRACE(fmt, ...) metalsharp::Logger::log(metalsharp::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define MS_INFO(fmt, ...)  metalsharp::Logger::log(metalsharp::LogLevel::Info, fmt, ##__VA_ARGS__)
#define MS_WARN(fmt, ...)  metalsharp::Logger::log(metalsharp::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define MS_ERROR(fmt, ...) metalsharp::Logger::log(metalsharp::LogLevel::Error, fmt, ##__VA_ARGS__)
