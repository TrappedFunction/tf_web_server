#include "utils/logger.h"
#include <thread>
#include <cerrno> // for errno
#include <cstdio> // for snprintf

// 全局变量定义
Logger::LogLevel g_logLevel = Logger::INFO;
void defaultOutput(const char* msg, int len) {
    fwrite(msg, 1, len, stdout);
}
Logger::OutputFunc g_output = defaultOutput;

// Logger::Impl 实现
Logger::Impl::Impl(LogLevel level, int saved_errno, const char* file, int line)
    : time_(Timestamp::now()),
      stream_(),
      level_(level),
      line_(line),
      basename_(file) {
    // 格式化时间
    formatTime();
    // 格式化线程ID (TODO 简化线程ID管理)
    stream_ << std::this_thread::get_id() << ' ';
    // 格式化日志级别
    const char* levelStr[Logger::NUM_LOG_LEVELS] = {"TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL "};
    stream_ << levelStr[level] << ' ';
}

void Logger::Impl::formatTime() {
    stream_ << time_.toString() << ' ';
}

void Logger::Impl::finish() {
    stream_ << " - " << basename_ << ':' << line_ << '\n';
}

// Logger 实现
Logger::Logger(const char* file, int line, LogLevel level, const char* func)
    : impl_(level, 0, file, line) {
    impl_.stream_ << func << ' ';
}

Logger::~Logger() {
    impl_.finish();
    const LogStream::Buffer& buf(stream().buffer());
    g_output(buf.data(), buf.length());
    if (impl_.level_ == FATAL) {
        g_output(buf.data(), buf.length()); // 确保FATAL信息被输出
        abort();
    }
}

void Logger::setOutput(OutputFunc out) {
    g_output = out;
}

void Logger::setLogLevel(LogLevel level) {
    g_logLevel = level;
}