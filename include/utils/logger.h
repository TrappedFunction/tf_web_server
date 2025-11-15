#pragma once
#include "utils/log_stream.h"
#include "utils/timestamp.h"
#include <functional>

// 负责在日志消息前加上时间戳、线程ID、日志级别等元信息
class Logger{
public:
    enum LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, FATAL, NUM_LOG_LEVELS };

    Logger(const char* file, int line, LogLevel level, const char* func = "");
    ~Logger();

    LogStream& stream() { return impl_.stream_; };

    static LogLevel logLevel();
    using OutputFunc = std::function<void(const char* msg, int len)>;
    static void setOutput(OutputFunc);
    static void setLogLevel(LogLevel level);

private:
    class Impl {
    public:
        Impl(LogLevel level, int old_errno, const char* file, int line);
        void formatTime();
        void finish();

        Timestamp time_;
        LogStream stream_;
        LogLevel level_;
        int line_;
        const char* fullname_;
        const char* basename_;
    };
    Impl impl_;
};

extern Logger::LogLevel g_logLevel;
inline Logger::LogLevel Logger::logLevel() { return g_logLevel; }

// 宏定义，方便日志调用
#define LOG_TRACE if (Logger::logLevel() <= Logger::TRACE) \
    Logger(__FILE__, __LINE__, Logger::TRACE).stream()
#define LOG_DEBUG if (Logger::logLevel() <= Logger::DEBUG) \
    Logger(__FILE__, __LINE__, Logger::DEBUG).stream()
#define LOG_INFO Logger(__FILE__, __LINE__, Logger::INFO).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, Logger::ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, Logger::FATAL).stream()