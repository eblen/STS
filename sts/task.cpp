#include "task.h"

bool SubTask::run() {
    auto runner = task_->getRunner(range_, timeData_);
    runner->wait();
    while (!runner->isFinished()) {
        runner->cont();
        runner->wait();
    }
    return true;
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
