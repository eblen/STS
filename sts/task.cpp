#include "task.h"

bool SubTask::run() {
    if (!task_->isCoroutine()) {
        task_->run(range_, timeData_);
        return true;
    }

    if (lr_.get() == nullptr) {
        lr_ = task_->getRunner(range_, timeData_);
    }
    else {
        lr_->cont();
    }
    lr_->wait();

    bool isDone = lr_->isFinished();
    if (isDone) {
        LRPool::gpool.release(lr_);
    }

    return isDone;
}
const Task* SubTask::getTask() const {
    return task_;
}
bool SubTask::isDone() const {
    return isDone_;
}
void SubTask::setDone(bool isDone) {
    isDone_ = isDone;
}
bool SubTask::isReady() const {
    return task_->isReady();
}
