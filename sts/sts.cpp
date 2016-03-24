#include <cassert>

#include <chrono>
#include <iostream>
#include <memory>

#include "sts.h"

std::unique_ptr<STS> STS::instance_(new STS());

STS::STS() {
    stepCounter_.store(0, std::memory_order_release);
    threads_.emplace_back(0);
}

STS::~STS() {
    //-1 notifies threads to finish
    stepCounter_.store(-1, std::memory_order_release);
    for(unsigned int i=1;i<threads_.size();i++) {
        threads_[i].join();
    }
}

void STS::setNumThreads(int n) {
    for (int id = threads_.size(); id < n; id++) {
        threads_.emplace_back(id); //create threads
    }
    for (int id = threads_.size(); id > n; id--) {
        threads_.pop_back();
    }
    if (bUseDefaultSchedule_) {
        clearAssignments();
        for (int id = 0; id < n; id++) {
            assign("default", id, {{id,     n},
                    {id + 1, n}});
        }
        bUseDefaultSchedule_ = true;
    }
}

void STS::assign(std::string label, int threadId, Range<Ratio> range) {
    int id = getTaskId(label);
    assert(range.start>=0 && range.end<=1);
    SubTask const* subtask = threads_.at(threadId).addSubtask(id, range);
    tasks_[id].pushSubtask(threadId, subtask);
    bUseDefaultSchedule_ = false;
}

void STS::clearAssignments() {
    for (auto &thread : threads_) {
        thread.clearSubtasks();
    }
    for (auto &task : tasks_) {
        task.clearSubtasks();
    }
}

void STS::nextStep() {
    assert(Thread::getId()==0);
    for (auto &task: tasks_) {
        task.functor_.reset(nullptr);
    }
    for (int i=0; i<threads_.size(); i++) {
        threads_[i].resetTaskQueue();
    }
    stepCounter_.fetch_add(1, std::memory_order_release);
}

void STS::reschedule() {
    // not yet available
}

void STS::wait() {
    if (!bUseDefaultSchedule_) {
        threads_[0].processQueue(); //Before waiting the OS thread executes its queue
        for(unsigned int i=1;i<threads_.size();i++) {
            threads_[i].wait();
        }
        if (bSTSDebug_) {
            std::cerr << "Times for step " << loadStepCounter() << std::endl;
            for (const auto &t : tasks_) {
                for (const auto &st : t.subtasks_) {
                    auto wtime = std::chrono::duration_cast<std::chrono::microseconds>(st->waitTime_).count();
                    auto rtime = std::chrono::duration_cast<std::chrono::microseconds>(st->runTime_).count();
                    std::cerr << getTaskLabel(st->getTaskId()) << " " << wtime << " " << rtime << std::endl;
                }
                if (t.subtasks_.size() > 1) {
                    auto ltwtime = std::chrono::duration_cast<std::chrono::microseconds>(t.waitTime_).count();
                    std::cerr << "Wait for task to complete " << ltwtime << std::endl;
                }
            }
        }
    }
}

ITaskFunctor *STS::getTaskFunctor(int taskId) {
    auto &func = tasks_[taskId].functor_;
    return func.wait();
}

int STS::getTaskNumThreads() {
    const Task *t = getCurrentTask();
    if (t == nullptr) {
        return 0;
    }
    return t->getNumThreads();
}

int STS::getTaskNumThreads(std::string label) {
    // TODO: Handle case where label is not a valid task.
    // Currently, it will insert a new task!
    int taskId = getTaskId(label);
    return tasks_[taskId].getNumThreads();
}

int STS::getTaskThreadId() {
    const Task *t = getCurrentTask();
    if (t == nullptr) {
        return -1;
    }
    int ttid = t->getThreadId(Thread::getId());
    // Would mean that thread is currently running a task it was never assigned.
    assert(ttid > -1);
    return ttid;
}

int STS::loadStepCounter() { return stepCounter_.load(std::memory_order_acquire); }

int STS::waitOnStepCounter(int c) {return wait_until_not(stepCounter_, c);}

const Task *STS::getCurrentTask() {
    int threadId = Thread::getId();
    int taskId = threads_[threadId].getCurrentTaskId();
    if (taskId == -1) {
        return nullptr;
    }
    return &tasks_[taskId];
}

int STS::getTaskId(std::string label) {
    auto it = taskLabels_.find(label);
    if (it != taskLabels_.end()) {
        return it->second;
    } else {
        assert(Thread::getId()==0); //creating thread should only be done by master thread
        unsigned int v = taskLabels_.size();
        assert(v==tasks_.size());
        tasks_.resize(v+1);
        taskLabels_[label] = v;
        return v;
    }
}

std::string STS::getTaskLabel(int id) const {
    for (auto it: taskLabels_) {
        if (it.second == id) return it.first;
    }
    throw std::invalid_argument("Invalid task Id: "+id);
}
