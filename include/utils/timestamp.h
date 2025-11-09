#pragma once
#include <chrono> // 时间测量、时间段计算
#include <string>

// 封装时间点
class Timestamp{
public:
    Timestamp() : micro_seconds_since_epoch_(0) {}
    explicit Timestamp(int64_t micro_seconds) : micro_seconds_since_epoch_(micro_seconds){}

    static Timestamp now();
    std::string toString() const;

    int64_t microSecondSinceEpoch() const { return micro_seconds_since_epoch_; }
    static const int kMicroSecondsPerSecond = 1000 * 1000;
private:
    int64_t micro_seconds_since_epoch_;
};

inline bool operator<(Timestamp lhs, Timestamp rhs){
    return lhs.microSecondSinceEpoch() < rhs.microSecondSinceEpoch();
}

inline Timestamp addTime(Timestamp timestamp, double seconds){
    int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
    return Timestamp(timestamp.microSecondSinceEpoch() + delta);
}