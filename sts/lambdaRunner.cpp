#include "lambdaRunner.h"

void setAffinity(int core) {
    cpu_set_t cpuMask;
    CPU_ZERO(&cpuMask);
    CPU_SET(core, &cpuMask);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuMask);
}

thread_local LambdaRunner* LambdaRunner::instance = nullptr;
