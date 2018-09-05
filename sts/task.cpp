#include "task.h"

bool SubTask::run() {
    if (!task_->isCoroutine(Thread::getId())) {
        task_->run(range_, runInfo_, timeData_);
        return true;
    }

    if (lr_.get() == nullptr) {
        lr_ = task_->getRunner(range_, runInfo_, timeData_);
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
