#ifndef STS_TASK_H
#define STS_TASK_H

#include <map>
#include <set>
#include <vector>

#include <chrono>

#include "barrier.h"
#include "range.h"

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

// Forward declaration
class SubTask;

/*! \internal \brief
 * A task to be executed
 *
 * Can either be a function or loop. Depending on the schedule is
 * executed synchronous or asynchronous. Functions are always
 * executed by a single thread. Loops are executed, depending on
 * the schedule, in serial or in parallel.
 */
struct Task {
    enum Type {RUN,LOOP};
    enum Priority {NORMAL, HIGH};
    Priority priority_;
    void *reduction_; //!< Reduction function to execute after task completes
    ITaskFunctor *functor_;      //!< The function/loop to execute
    MOBarrier functorBeginBarrier_; //!< Many-to-one barrier to sync threads at beginning of loop
    OMBarrier functorEndBarrier_; //!< One-to-many barrier to sync threads at end of loop
    //! All subtasks of this task. One for each section of a loop. One for a basic task.
    std::vector<SubTask*> subtasks_; //!< Subtasks to be executed by a single thread
    //!< The waiting time in the implied barrier at the end of a loop. Zero for basic task.
    sts_clock::duration waitTime_; //!< Time that main thread waits on end barrier
    sts_clock::duration reductionTime_; //!< Time spent doing reduction

    Task(Type t, const std::set<int> &uaTaskThreads = {}) :priority_(NORMAL),
    reduction_(nullptr), functor_(nullptr), waitTime_(0), reductionTime_(0), type_(t),
    numThreads_(0), uaTaskThreads_(uaTaskThreads) {}
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
    Type getType() const {
        return type_;
    }
    const std::set<int> &getUATaskThreads() const {
        return uaTaskThreads_;
    }
private:
    Type type_;
    int numThreads_;
    //! Threads used for executing unassigned tasks
    //! Note: Only relevant for RUN tasks (LOOP tasks cannot contain other tasks)
    std::set<int> uaTaskThreads_;
    //! Map STS thread id to an id only for this task (task ids are consecutive starting from 0)
    //! Note: Only useful for LOOP tasks (RUN tasks only have one thread)
    std::map<int, int> threadTaskIds_;
};

/*! \internal \brief
 * The portion of a task done by one thread
 *
 * Contains all data and functions needed to execute the subtask, along with
 * timing variables that can be used to record wait and run times.
 */
class SubTask {
public:
    enum Type {ASSIGNED, UNASSIGNED, EITHER};
    /*! \brief
     * Constructor
     *
     * \param[in] task     The task this is part of.
     * \param[in] range    Out of a possible range from 0 to 1, the section in
                           this part. Ignored for basic tasks.
     */
    SubTask(Task &task, Range<Ratio> range) : finished(false), range_(range),
    task_(task) {}
    /*! \brief
     * Test if functor is available to execute without waiting
     *
     * \return whether functor is ready
     */
    bool isReady() const {
        return task_.functorBeginBarrier_.test();
    }
    /*! \brief
     * Get the functor for the subtask.
     * Threads should execute the functor on the range. Note that this function
     * may wait for the corresponding run/parallel_for section to be encountered
     * and then assigned by the main thread.
     */
    ITaskFunctor *getFunctor() {
        task_.functorBeginBarrier_.wait();
        return task_.functor_;
    }
    const Task &getTask() const {
        return task_;
    }
    /*! \brief
     * Mark work as completed.
     * Threads must always call this after completing the functor so that main
     * thread can proceed.
     */
    void markComplete() {
        task_.functorEndBarrier_.markArrival();
        finished = true;
    }

    sts_clock::duration waitTime_; /**< Time spent until task was ready */
    sts_clock::duration runTime_;  /**< Time spent executing subtask  */
    bool finished;
    const Range<Ratio> range_;     /**< Range (out of [0,1]) of loop part */
private:
    Task &task_;             /**< Reference to main task */
};

#endif // STS_TASK_H
