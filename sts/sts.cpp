#include "sts.h"

std::deque<Thread> STS::threads_ = {};
std::atomic<int> STS::stepCounter_ = 0;
STS* STS::instance_ = nullptr;
