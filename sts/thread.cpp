#include "thread.h"

#include "sts.h"

int thread_local Thread::id_ = 0;

void Thread::doWork() {
    for (int i=0; ; i++) {
        int c = STS::waitOnStepCounter(i);
        if (c<0) break; //negative task counter signals to terminate the thread
        processQueue();
    }
}

void Thread::processQueue() {
    currentScheduleName_ = STS::getCurrentInstance()->id;
    while(processTask());
    assert(currentScheduleName_ == STS::getCurrentInstance()->id);
}

bool Thread::processTask() {
    STS* sts = STS::getCurrentInstance();
    assert(currentScheduleName_ == sts->id);
    SubTask* subtask = sts->AdvanceToNextSubTask(id_);
    if (subtask == nullptr) {
        return false;
    }
    auto startWaitTime = sts_clock::now();
    ITaskFunctor *task = sts->getTaskFunctor(subtask->getTaskId());
    auto startTaskTime = sts_clock::now();
    subtask->waitTime_ = startTaskTime - startWaitTime;
    task->run(subtask->getRange());
    subtask->runTime_ = sts_clock::now() - startTaskTime;
    sts->markSubtaskComplete(subtask->getTaskId());
    return true;
}
