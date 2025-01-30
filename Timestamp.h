#ifndef _TIMESTAMP_H_
#define _TIMESTAMP_H_

#include <iostream>
#include <string>

// 时间类
class Timestamp
{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpochArg);
    static Timestamp now();
    std::string toString() const;
private:
    int64_t microSecondsSinceEpoch_;
};

#endif