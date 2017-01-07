#include "thread.h"

#include "sts.h"

#include <string>

int thread_local Thread::id_ = 0;

void Thread::doWork() {
    for (int i=0; ; i++) {
        int c = STS::waitOnStepCounter(i);
        if (c<0) break; //negative task counter signals to terminate the thread
        processQueue();
    }
}

void Thread::processQueue() {
    // STS schedule should never change while processing queue
    STS* sts = STS::getCurrentInstance();
    std::string startScheduleName = sts->id;
    SubTask* subtask = sts->advanceToNextSubTask(id_);
    while(subtask != nullptr) {
        STS* sts = STS::getCurrentInstance();
        assert(sts->id == startScheduleName);
        if (subtask->run()) {
            subtask = sts->advanceToNextSubTask(id_);
        }
    }
    assert(STS::getCurrentInstance()->id == startScheduleName);
}
