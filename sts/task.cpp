#include "task.h"
 
SubTaskRunInfo::SubTaskRunInfo(Task* t) :isRunning(false), startIter(0),
endIter(0), currentIter(0), mutex(t->getAutoBalancingMutex()) {}

bool SubTask::runImpl() {
    if (!task_->isCoroutine(Thread::getId())) {
        if (doingExtraWork_) {
            task_->run(workingRange_, runInfo_, timeData_);
        }
        else {
            task_->run(range_, runInfo_, timeData_);
        }
        return true;
    }

    if (lr_.get() == nullptr) {
        if (doingExtraWork_) {
            lr_ = task_->getRunner(workingRange_, runInfo_, timeData_);
        }
        else {
            lr_ = task_->getRunner(range_, runInfo_, timeData_);
        }
    }
    else {
        // Runner stores start and finish times but not intermediate pauses
        // and restarts.
        timeData_.runStart.push_back(sts_clock::now());
        lr_->cont();
    }
    lr_->wait();

    bool isDone = lr_->isFinished();
    if (isDone) {
        LRPool::gpool.release(lr_);
    }
    else {
        timeData_.runEnd.push_back(sts_clock::now());
    }

    return isDone;
}

bool SubTask::run() {
    while(true) {
        bool done = runImpl();
        // Only attempt to steal work when subtask completes current work
        if (!done) {
            return false;
        }
        else {
            if (task_->stealWork(*this)) {
                // Only true until stealing fails
                doingExtraWork_ = true;
            }
            else {
                // Must reset flag for next step
                doingExtraWork_ = false;
                return true;
            }
        }
    }
}

Task* SubTask::getTask() const {
    return task_;
}
bool SubTask::isDone() const {
    return isDone_;
}
void SubTask::setDone(bool isDone) {
    isDone_ = isDone;
}
void SubTask::setCheckPoint(int cp) {
    task_->setCheckPoint(cp);
}
void SubTask::waitForCheckPoint() const {
    task_->waitForCheckPoint(checkPoint_);
}
bool SubTask::isReady() const {
    return task_->isReady();
}
