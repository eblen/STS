#include "sts.h"

int thread_local Thread::id_ = 0;
STS *STS::instance_ = nullptr;

void Thread::doWork() {
    STS *sts = STS::getInstance();
    for (int i=0; ; i++) {
        int c;
        while ((c=sts->loadStepCounter())==i); //wait on current step to finish
        resetTaskQueue();
        if (c<0) break; //negative task counter signals to terminate the thread
        processQueue();
    }
}

void Thread::processQueue() { 
    int s = taskQueue_.size();
    while(nextSubtaskId_<s) {
        processTask();
    }
    nextSubtaskId_=0;
}

void Thread::processTask() {
    auto& subtask = taskQueue_[nextSubtaskId_++];
    auto startWaitTime = sts_clock::now();
    ITaskFunctor *task = STS::getInstance()->getTaskFunctor(subtask.taskId_);
    auto startTaskTime = sts_clock::now();
    subtask.waitTime_ = startTaskTime - startWaitTime;
    task->run(subtask.range_);
    subtask.runTime_ = sts_clock::now() - startTaskTime;
    subtask.setDone(true);
}

//This is currently just a manual schedule for the example. Automatic scheduling isn't done yet.
void STS::reschedule()
{
    clearAssignments();
    bUseDefaultSchedule_ = false;

    assign("TASK_F", 1);
    assign("TASK_G", 2); 

    assign("TASK_F_0", 1, {0, {2,3}}); 
    
    assign("TASK_G_0", 2, {0, {1,2}});
    assign("TASK_G_1", 2, {0, {1,2}});
    
    assign("TASK_G_0", 0, {{1,2}, 1});
    assign("TASK_F_0", 0, {{2,3}, 1});
    assign("TASK_G_1", 0, {{1,2}, 1});

    nextStep();
}
