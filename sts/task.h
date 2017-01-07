#ifndef STS_TASK_H
#define STS_TASK_H

#include <map>
#include <set>
#include <vector>

#include <chrono>

#include "barrier.h"
#include "range.h"
#include "thread.h"

using sts_clock = std::chrono::steady_clock;

//! \internal Interface of the executable function of a task
class ITaskFunctor {
public:
    /*! \brief
     * Run the function of the task
     *
     * \param[in] range  range of task to be executed. Ignored for basic task.
     */
    virtual void run(Range<Ratio> range) = 0;
    virtual ~ITaskFunctor() {};
};

//! \internal Loop task functor
template<class F>
class LoopTaskFunctor : public ITaskFunctor {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] f    lambda of loop body
     * \param[in] r    range of loop
     */
    LoopTaskFunctor<F>(F f, Range<int64_t> r): body_(f), range_(r) {}
    void run(Range<Ratio> r) {
        Range<int64_t> s = range_.subset(r); //compute sub-range of this execution
        for (int i=s.start; i<s.end; i++) {
            body_(i);
        }
    }
private:
    F body_;
    Range<int64_t> range_;
};

//! \internal Basic (non-loop) task functor
template<class F>
class BasicTaskFunctor : public ITaskFunctor {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] f    lambda of function
     */
    BasicTaskFunctor<F>(F f) : func_(f) {};
    void run(Range<Ratio>) {
        func_();
    }
private:
    F func_;
};

class Task;

/*! \internal \brief
 * The portion of a task done by one thread
 *
 * Contains all data and functions needed to execute the subtask, along with
 * timing variables that can be used to record wait and run times.
 */
class SubTask {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] task     The task this is part of.
     * \param[in] range    Out of a possible range from 0 to 1, the section in
                           this part. Ignored for basic tasks.
     */
    SubTask(Task *task, Range<Ratio> range) : task_(task), range_(range),
    isDone_(false) {}
    /*! \brief
     * Run the subtask
     */
    bool run();
    const Task *getTask() const;
    bool isDone() const;
    void setDone(bool isDone);

    const Range<Ratio> range_;     /**< Range (out of [0,1]) of loop part */
private:
    Task *task_;             /**< Reference to main task */
    bool isDone_;
};

/*! \internal \brief
 * Root task class that handles the storage and bookkeeping of subtasks,
 * operations needed by all task classes.
 *
 * Each subtask is assigned to a single thread, and this class assigns a
 * Task-specific thread id to all participating threads. Subclasses store
 * the actual function and handle execution of the task.
 *
 * Note that for non-loop tasks, only a single subtask and thread are
 * needed.
 */
class Task {
public:
    Task() :numThreads_(0), reduction_(nullptr) {}
    /*! \brief
     * Add a new subtask for this task
     *
     * \param[in] threadId  thread to which this subtask is assigned
     * \param[in] t         subtask
     */
    void pushSubtask(int threadId, SubTask* t) {
        subtasks_.push_back(t);
        if (threadTaskIds_.find(threadId) == threadTaskIds_.end()) {
            threadTaskIds_[threadId] = numThreads_;
            numThreads_++;
        }
    }
    //! \brief Remove all subtasks for this task
    void clearSubtasks() {
        subtasks_.clear();
        threadTaskIds_.clear();
        numThreads_ = 0;
    }
    //! \brief Get total number of threads assigned to some subtask for this task
    int getNumThreads() const {
        return numThreads_;
    }
    /*! \brief
     * Get task-specific thread Id for the given STS thread Id
     *
     * \param[in] threadId  STS thread id
     */
    int getThreadId(int threadId) const {
        auto id = threadTaskIds_.find(threadId);
        if (id == threadTaskIds_.end()) {
            return -1;
        }
        return (*id).first;
    }
    void restart() {
        for (SubTask* st : subtasks_) {
            st->setDone(false);
        }
    }
    void* getReduction() const {
        return reduction_;
    }
    void setReduction(void* r) {
        reduction_ = r;
    }
    virtual void  setFunctor(ITaskFunctor *) = 0;
    virtual bool  run(Range<Ratio>) = 0;
    virtual void  wait() = 0;
    virtual void  clear() = 0;
    virtual ~Task() {}
protected:
    void* reduction_;
private:
    //! All subtasks of this task. One for each section of a loop. One for a basic task.
    std::vector<SubTask*> subtasks_; //!< Subtasks to be executed by a single thread
    int numThreads_;
    //! Map STS thread id to an id only for this task (task ids are consecutive starting from 0)
    std::map<int, int> threadTaskIds_;
};

class LoopTask : public Task {
public:
    LoopTask() : Task(), functor_(nullptr) {}
    void setFunctor(ITaskFunctor *f) {
        functor_ = f;
        functorBeginBarrier_.open();
    }
    bool run(Range<Ratio> range) {
        functorBeginBarrier_.wait();
        functor_->run(range);
        functorEndBarrier_.markArrival();
        return true;
    }
    void wait() {
        functorEndBarrier_.wait();
    }
    void clear() {
        functor_ = nullptr;
        functorBeginBarrier_.close();
        functorEndBarrier_.close(this->getNumThreads());
    }
private:
    ITaskFunctor *functor_;      //!< The function/loop to execute
    MOBarrier functorBeginBarrier_; //!< Many-to-one barrier to sync threads at beginning of loop
    OMBarrier functorEndBarrier_; //!< One-to-many barrier to sync threads at end of loop
};

class MultiLoopTask : public Task {
public:
    MultiLoopTask() : Task(), functor_(nullptr),
    functorCounter_(0) {}
    void setFunctor(ITaskFunctor *f) {
        functorEndBarrier_.close(this->getNumThreads());
        functor_ = f;
        functorCounter_++;
    }
    void markFinished() {
        functorCounter_ = -1;
    }
    bool run(Range<Ratio> range) {
        int localThreadId = this->getThreadId(Thread::getId());
        wait_until_not(functorCounter_, threadCounters_[localThreadId]++);
        if (functorCounter_ == -1) {
            return true;
        }
        functor_->run(range);
        functorEndBarrier_.markArrival();
        return false;
    }
    void wait() {
        functorEndBarrier_.wait();
    }
    void clear() {
        functorCounter_ = 0;
        functor_ = nullptr;
        functorEndBarrier_.close(this->getNumThreads());
        threadCounters_.assign(this->getNumThreads(),0);
    }
private:
    ITaskFunctor *functor_;      //!< The function/loop to execute
    OMBarrier functorEndBarrier_; //!< One-to-many barrier to sync threads at end of loop
    std::atomic<int> functorCounter_;
    std::vector<int> threadCounters_;
};

class BasicTask : public Task {
public:
    BasicTask() : Task(), functor_(nullptr), containedMultiLoopTask_(nullptr) {}
    void setFunctor(ITaskFunctor *f) {
        functor_ = f;
        functorBeginBarrier_.open();
    }
    bool run(Range<Ratio> range) {
        functorBeginBarrier_.wait();
        functor_->run(range);
        functorEndBarrier_.markArrival();
        if (containedMultiLoopTask_ != nullptr) {
            containedMultiLoopTask_->markFinished();
        }
        return true;
    }
    void setMultiLoop(MultiLoopTask *mlt) {
        assert(containedMultiLoopTask_ == nullptr);
        containedMultiLoopTask_ = mlt;
    }
    void wait() {
        functorEndBarrier_.wait();
    }
    void clear() {
        functor_ = nullptr;
        functorBeginBarrier_.close();
        functorEndBarrier_.close(this->getNumThreads());
    }
private:
    ITaskFunctor *functor_;      //!< The function/loop to execute
    MOBarrier functorBeginBarrier_; //!< Many-to-one barrier to sync threads at beginning of loop
    OMBarrier functorEndBarrier_; //!< One-to-many barrier to sync threads at end of loop
    MultiLoopTask* containedMultiLoopTask_;
};

#endif // STS_TASK_H
