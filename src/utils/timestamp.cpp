#include "utils/timestamp.h"
#include <sys/time.h>
#include <cstdio>

Timestamp Timestamp::now(){
    struct timeval tv;
    // gettimeofday是获取微秒级精度的方法
    gettimeofday(&tv, NULL);
    int64_t seconds = tv.tv_sec;
    int64_t micro_seconds = seconds * kMicroSecondsPerSecond + tv.tv_usec;
    return Timestamp(micro_seconds);
}

std::string Timestamp::toString() const {
    char buf[64] = {0};
    int64_t seconds = micro_seconds_since_epoch_ / kMicroSecondsPerSecond;
    int64_t micro_seconds = micro_seconds_since_epoch_ % kMicroSecondsPerSecond;

    // 使用tm结构体和strftime进行格式化
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);

    snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d.%06ld",
        tm_time.tm_year + 1900,
        tm_time.tm_mon + 1,
        tm_time.tm_mday,
        tm_time.tm_hour,
        tm_time.tm_min,
        tm_time.tm_sec,
        micro_seconds);
    return buf;
}