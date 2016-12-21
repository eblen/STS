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
    SubTask* subtask = sts->advanceToNextSubTask(id_);
    if (subtask == nullptr) {
        return false;
    }
    subtask->run();
    return true;
}
