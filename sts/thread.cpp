#include <sched.h>

#include "thread.h"

#include "sts.h"

int thread_local Thread::id_ = 0;

void Thread::setAffinity(int coreId) {
    cpu_set_t mask;
    int status;

    CPU_ZERO(&mask);
    CPU_SET(coreId, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0)
    {
        perror("sched_setaffinity");
    }
}

void Thread::doWork() {
    for (int i=0; ; i++) {
        int c = STS::getInstance()->waitOnStepCounter(i);
        if (c<0) break; //negative task counter signals to terminate the thread
        processQueue();
    }
}

void Thread::processQueue() {
    unsigned int s = STS::getInstance()->getNumSubTasks(Thread::id_);
    while(nextSubtaskId_<s) {
        processTask();
    }
    nextSubtaskId_=0;
}

void Thread::processTask() {
    SubTask* subtask = STS::getInstance()->getSubTask(Thread::id_, nextSubtaskId_++);
    auto startWaitTime = sts_clock::now();
    ITaskFunctor *task = STS::getInstance()->getTaskFunctor(subtask->getTaskId());
    auto startTaskTime = sts_clock::now();
    subtask->waitTime_ = startTaskTime - startWaitTime;
    task->run(subtask->getRange());
    subtask->runTime_ = sts_clock::now() - startTaskTime;
    STS::getInstance()->markSubtaskComplete(subtask->getTaskId());
}

