#include "utils/logfile.h"
#include <cstdio>
#include <cassert>
#include <unistd.h> // for getpid
#include <sys/time.h>

LogFile::LogFile(const std::string& basename,
                 off_t roll_size,
                 int flush_interval,
                 int check_every_n)
    : basename_(basename),
      roll_size_(roll_size),
      flush_interval_(flush_interval),
      check_every_n_(check_every_n),
      count_(0),
      mutex_(new std::mutex),
      start_of_period_(0),
      last_roll_(0),
      last_flush_(0),
      file_(nullptr) {
    assert(basename.find('/') == std::string::npos); // 文件名不应包含路径
    rollFile();
}

LogFile::~LogFile() {
    if (file_) {
        ::fclose(file_);
    }
}

void LogFile::append(const char* logline, int len) {
    std::lock_guard<std::mutex> lock(*mutex_);
    append_unlocked(logline, len);
}

void LogFile::flush() {
    std::lock_guard<std::mutex> lock(*mutex_);
    ::fflush(file_);
}

void LogFile::append_unlocked(const char* logline, int len) {
    ::fwrite(logline, 1, len, file_);
    off_t written_bytes = ftell(file_); // 获取当前文件大小

    if (written_bytes > roll_size_) {
        rollFile();
    } else {
        ++count_;
        if (count_ >= check_every_n_) {
            count_ = 0;
            time_t now = ::time(NULL);
            time_t this_period = now / kRollPerSeconds_ * kRollPerSeconds_;
            if (this_period != start_of_period_) {
                rollFile();
            } else if (now - last_flush_ > flush_interval_) {
                last_flush_ = now;
                ::fflush(file_);
            }
        }
    }
}

void LogFile::rollFile() {
    time_t now = 0;
    std::string filename = getLogFileName(basename_, &now);
    
    // a new day, reset start_of_period_
    time_t this_period = now / kRollPerSeconds_ * kRollPerSeconds_;
    if (now > last_roll_) {
        last_roll_ = now;
        last_flush_ = now;
        start_of_period_ = this_period;
        
        if (file_) {
            ::fclose(file_);
        }
        
        file_ = ::fopen(filename.c_str(), "ae"); // 'e' for O_CLOEXEC
        if (!file_) {
            // Log FATAL: cannot open log file
            fprintf(stderr, "LogFile::rollFile() failed to open %s\n", filename.c_str());
            abort();
        }
    }
}

std::string LogFile::getLogFileName(const std::string& basename, time_t* now) {
    std::string filename;
    filename.reserve(basename.size() + 64);
    filename = basename;

    char timebuf[32];
    struct tm tm;
    *now = time(NULL);
    localtime_r(now, &tm); // 线程安全
    strftime(timebuf, sizeof(timebuf), ".%Y%m%d-%H%M%S.", &tm);
    filename += timebuf;

    char pidbuf[32];
    snprintf(pidbuf, sizeof(pidbuf), "%d", ::getpid());
    filename += pidbuf;

    filename += ".log";

    return filename;
}
