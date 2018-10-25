#ifndef STS_TASK_H
#define STS_TASK_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>

#include <chrono>

#include "barrier.h"
#include "range.h"
#include "thread.h"
#include "lambdaRunner.h"
#include "lrPool.h"

using namespace std::chrono;
using sts_clock = steady_clock;
static const auto STS_MAX_TIME_POINT = time_point<sts_clock>::max();

class Task;

/*! \brief
 * Class to store the state of a running subtask.
 * Specifically, store the start, end, and current iteration values of a loop
 * task, which can change concurrently if load balancing is being done.
 */
struct SubTaskRunInfo {
    bool isRunning;
    int64_t startIter;
    int64_t endIter;
    int64_t currentIter;
    std::mutex &mutex;
    SubTaskRunInfo(Task* t);
};

//! \internal Interface of the executable function of a task
class ITaskFunctor {
public:
    /*! \brief
     * Run the function of the task
     *
     * \param[in] range  range of task to be executed. Ignored for basic task.
     * \param[in] ri     structure for functor to share run information with caller.
     */
    virtual void run(Range<int64_t> range, SubTaskRunInfo &ri, bool itersCanChange) = 0;
    virtual void run(Range<Ratio>   range, SubTaskRunInfo &ri, bool itersCanChange) = 0;
    virtual ~ITaskFunctor() {};
};

//! \internal Loop task functor
template<typename F>
class LoopTaskFunctor : public ITaskFunctor {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] f    lambda of loop body
     * \param[in] r    range of loop
     */
    LoopTaskFunctor<F>(F f, Range<int64_t> r): body_(f), range_(r) {}
    /*! \brief
     * Run a task multiple times for a range of indices
     *
     * Note that run information can be concurrently altered, because iteration
     * values may be changed to support various load balancing strategies.
     *
     * Setting itersCanChange to false when load balancing is not being used can
     * significantly improve performance, especially for long ranges of short
     * iterations.
     *
     * \param[in] r                range of loop
     * \param[in] ri               run information
     * \param[in] itersCanChange   whether loop iteration values can change
     */
    void run(Range<int64_t> r, SubTaskRunInfo &ri, bool itersCanChange = true) {
        int64_t ci;
        bool finished = false;

        if (itersCanChange) {
            ri.mutex.lock();
        }
        ri.startIter   = r.start;
        ri.currentIter = r.start;
        ci             = r.start;
        ri.endIter     = r.end;
        ri.isRunning   = ci < ri.endIter;
        finished       = !ri.isRunning;
        if (itersCanChange) {
            ri.mutex.unlock();
        }

        while (!finished) {
            body_(ci);
            if (itersCanChange) {
                ri.mutex.lock();
            }
            ri.currentIter++;
            ci = ri.currentIter;
            ri.isRunning = ci < ri.endIter;
            finished = !ri.isRunning;
            if (itersCanChange) {
                ri.mutex.unlock();
            }
        }
    }
    void run(Range<Ratio> r, SubTaskRunInfo &ri, bool itersCanChange = true) {
        Range<int64_t> s = range_.subset(r); //compute sub-range of this execution
        run(s, ri, itersCanChange);
    }
private:
    F body_;
    Range<int64_t> range_;
};

//! \internal Allows creating a loop functor without knowing the function type
template<typename F>
LoopTaskFunctor<F>* createLoopTaskFunctor(F f, Range<int64_t> r) {
    return new LoopTaskFunctor<F>(f,r);
}

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
    void run(Range<int64_t>, SubTaskRunInfo &ri, bool itersCanChange = true) {
        if (itersCanChange) {
            ri.mutex.lock();
        }
        ri.startIter   = 0;
        ri.endIter     = 1;
        ri.currentIter = 0;
        if (itersCanChange) {
            ri.mutex.unlock();
        }
        func_();
        if (itersCanChange) {
            ri.mutex.lock();
        }
        ri.currentIter = 1;
        if (itersCanChange) {
            ri.mutex.unlock();
        }
    }
    void run(Range<Ratio>, SubTaskRunInfo &ri, bool itersCanChange = true) {
        run(Range<int64_t>({0,1}), ri, itersCanChange);
    }
private:
    F func_;
};

//! \internal Allows creating a basic functor without knowing the function type
template<typename F>
BasicTaskFunctor<F>* createBasicTaskFunctor(F f) {
    return new BasicTaskFunctor<F>(f);
}

struct TaskTimes {
public:
    time_point<sts_clock> waitStart;    // Time when work requested
    std::vector< time_point<sts_clock> > runStart; // Times when work started
    std::vector< time_point<sts_clock> > runEnd;   // Times when work finished
    time_point<sts_clock> nextRunAvail; // Time when next run (subtask) was ready
    std::map< std::string, std::vector<time_point<sts_clock>> > auxTimes;
    void clear() {
        waitStart    = STS_MAX_TIME_POINT;
        runStart.clear();
        runEnd.clear();
        nextRunAvail = STS_MAX_TIME_POINT;
        auxTimes.clear();
    }
    TaskTimes() {
       clear();
    }
};

/*! \internal \brief
 * The portion of a task done by one thread
 *
 * Contains run() method to execute subtask directly, along with needed
 * information (task and range).
 */
class SubTask {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] tid      Id of thread assigned the subtask
     * \param[in] task     The task this is part of.
     * \param[in] range    Out of a possible range from 0 to 1, the section in
                           this part. Ignored for basic tasks.
     */
    SubTask(int tid, Task *task, Range<Ratio> range) :threadId_(tid),
    task_(task), runInfo_(task), range_(range), lr_(nullptr), isDone_(false),
    checkPoint_(0), doingExtraWork_(false) {}
    /*! \brief
     * Reset the subtask for another run
     */
    void reset() {
        isDone_ = false;
        checkPoint_ = 0;
        timeData_.clear();
    }
    /*! \brief
     * Run the subtask
     *
     * \return whether task completed
     */
    bool run();
    /*! \brief
     * Pause the subtask
     * Input parameter can be safely ignored if not using checkpoints.
     *
     * \param[in] cp  checkpoint when it is okay to resume task.
     */
    void pause(int cp=0) {
        checkPoint_ = cp;
        LambdaRunner::instance->pause();
    }
    int getCheckPoint() const {
        return checkPoint_;
    }
    Task *getTask() const;
    bool isDone() const;
    void setDone(bool isDone);
    /*! \brief
     * Set checkpoint for containing Task (not SubTask)
     * This function exists because Tasks cannot be modified directly.
     *
     * \param[in] cp  checkpoint
     */
    void setCheckPoint(int cp);
    /*! \brief
     * Wait for checkpoint to be reached
     * This function exists for convenience (calls Task version of this
     * function with the correct cp value)
     */
    void waitForCheckPoint() const;
    bool isReady() const;
    long getWaitStartTime() const {
        auto s = time_point_cast<microseconds>(timeData_.waitStart);
        return s.time_since_epoch().count();
    }
    const std::vector<long> getRunStartTimes() const {
        std::vector<long> times;
        for (auto t : timeData_.runStart) {
            auto ms = time_point_cast<microseconds>(t);
            times.push_back(ms.time_since_epoch().count());
        }
        return times;
    }
    const std::vector<long> getRunEndTimes() const {
        std::vector<long> times;
        for (auto t : timeData_.runEnd) {
            auto ms = time_point_cast<microseconds>(t);
            times.push_back(ms.time_since_epoch().count());
        }
        return times;
    }
    long getNextRunAvailTime() const {
        auto s = time_point_cast<microseconds>(timeData_.nextRunAvail);
        return s.time_since_epoch().count();
    }
    /*! \brief
     * Set time when the next subtask became available (lambda was set)
     *
     * \param[in] t time in microseconds since epoch
     */
    void setNextRunAvailTime(long t) {
       timeData_.nextRunAvail = time_point<sts_clock>(microseconds(t));
    }
    void recordTime(std::string label) {
        timeData_.auxTimes[label].push_back(sts_clock::now());
    }
    std::vector<long> getAuxTimes(std::string label) const {
        std::vector<long> times;
        for (auto t : timeData_.auxTimes.at(label)) {
            auto s = time_point_cast<microseconds>(t);
            times.push_back(s.time_since_epoch().count());
        }
        return times;
    }
    Range<Ratio> getRange() const {
        return range_;
    }
    void setRange(Range<Ratio> r) {
        range_ = r;
    }
    void setWorkingRange(Range<int64_t> r) {
        workingRange_ = r;
    }
    SubTaskRunInfo getRunInfo() const {
        return runInfo_;
    }
    void setEndIter(int64_t i) {
        runInfo_.endIter = i;
    }
    const int threadId_;
private:
    bool runImpl();
    Task *task_;             /**< Reference to main task */
    SubTaskRunInfo runInfo_; /**< Info reported during run */
    Range<Ratio> range_;     /**< Range (out of [0,1]) of loop part */
    // Working range. Can change during step and used to store additional
    // ranges found from work stealing while auto balancing. Stores actual
    // iteration values rather than ratios.
    Range<int64_t> workingRange_;
    std::unique_ptr<LambdaRunner> lr_;
    bool isDone_;
    TaskTimes timeData_;
    // Check point when subtask should be resumed. Resets to zero at the
    // beginning of each step. This can be used, along with the Task class
    // checkpoints, to avoid resuming a coroutine until a certain event
    // completes, such as a lengthy communication. STS will not resume the
    // subtask on a pause until the main task has reached the set checkpoint.
    // Thus, the application does not need extra logic to avoid pausing too
    // early or too often (as long as STS no-op pauses are fast).
    int checkPoint_;
    // Record times when run starts, pauses, and finishes
    std::vector< time_point<sts_clock> > times_;
    // Keep track of when extra work is being done instead of the assigned work (used for auto balancing).
    bool doingExtraWork_;
};

/*! \internal \brief
 * Task class for both loops and non-loops.
 *
 * Tasks are made up of subtasks, one for a non-loop task and one or more for
 * a loop task. Each subtask is done by a single thread, and this class assigns
 * a task-specific thread id to all participating threads. These ids start at
 * zero and are contiguous.
 *
 * Note that for non-loop tasks, only a single subtask and thread are needed,
 * and thus much of the infrastructure in this class is superfluous.
 */
class Task {
public:
    Task(std::string l) :reduction_(nullptr), label(l), numThreads_(0),
                         functorSetTime_(STS_MAX_TIME_POINT), functor_(nullptr),
                         checkPoint_(0), autoBalancing_(false) {}
    /*! \brief
     * Add a new subtask for this task
     *
     * Note: Task takes ownership of passed subtask.
     *
     * \param[in] threadId  thread to which this subtask is assigned
     * \param[in] t         subtask
     */
    void pushSubtask(int threadId, SubTask* t) {
        subtasks_.emplace_back(t);
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
    //! \brief Get total number of subtasks for this task
    int getNumSubtasks() const {
        return subtasks_.size();
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
        return (*id).second;
    }
    /*! \brief
     * Restart the task. Must be called on each task prior to starting work,
     * normally called for all tasks in an STS schedule at the beginning of
     * each step.
     */
    void restart() {
        for (std::unique_ptr<SubTask> &st : subtasks_) {
            st->reset();
        }
        init();
    }
    //! \brief Get task label
    std::string getLabel() const {
        return label;
    }
    const std::set<std::string>& getNextTasks() const {
        return nextTasks_;
    }
    void setCoroutine(const std::vector<int>& threads,
    const std::set<std::string> &continuations) {
        for (int t : threads) {
            assert(t > -1);
            runAsCoroutine_.insert(t);
        }
        nextTasks_ = continuations;
    }
    bool isCoroutine(int tid) const {
        assert(tid > -1);
        return runAsCoroutine_.find(tid) != runAsCoroutine_.end();
    }
    //! \brief Get reduction function
    void* getReduction() const {
        return reduction_;
    }
    /*! \brief
     * Store a new reduction function
     *
     * This class only stores the reduction. Clients and subclasses are
     * responsible for making use of the reduction.
     *
     * \param[in] r reduction function
     */
    void setReduction(void* r) {
        reduction_ = r;
    }
    const SubTask* getSubTask(size_t i) const {
        if (i < subtasks_.size()) {
            return subtasks_[i].get();
        }
        return nullptr;
    }
    /*! \brief
     * Set ranges for the subtasks of a task
     *
     * \param[in] intervals vector of ratios marking start and end points for each subtask
     * Example: setSubTaskRanges("reduce", {0,{1,6},{3,6},{4,6},1}
     */
    void setSubTaskRanges(std::vector<Ratio> intervals) {
        assert(intervals.size() == subtasks_.size()+1);
        assert(intervals[0] == 0);
        assert(intervals.back() == 1);
        for (size_t i=0; i<subtasks_.size(); i++) {
            assert(intervals[i] <= intervals[i+1]);
            subtasks_[i]->setRange({intervals[i],intervals[i+1]});
        }
    }
    void enableAutoBalancing() {
        autoBalancing_ = true;
    }
    std::mutex& getAutoBalancingMutex() {
        return autoBalancingMutex_;
    }
    /*! \brief
     * Set task checkpoint
     *
     * \param[in] cp  checkpoint
     */
    void setCheckPoint(int cp) {
        checkPoint_ = cp;
    }
    int getCheckPoint() const {
        return checkPoint_;
    }
    /*! \brief
     * Wait for task to reach the given checkpoint
     *
     * \param[in] cp  checkpoint
     */
    void waitForCheckPoint(int cp) const {
        if (checkPoint_ < cp) {
            wait_until_ge(checkPoint_,cp);
        }
    }
    /*! \brief
     * Set the functor (work) to be done.
     *
     * In the base class, we simply log the time of setting the functor
     * (important for load balancing) and call the subclass implementation.
     *
     * Note: Implementatons assume ownership of the passed functor.
     *
     * \param[in] f Task functor
     */
    void setFunctor(ITaskFunctor* f) {
        setFunctorImpl(f);
        functorSetTime_ = sts_clock::now();
    }
    long getFunctorSetTime() const {
        auto s = time_point_cast<microseconds>(functorSetTime_);
        return s.time_since_epoch().count();
    }
    /*! \brief
     * Return whether task is ready to run
     */
    bool isReady() const {
        return functorBeginBarrier_.isOpen();
    }
    template <typename R>
    void run(R range, SubTaskRunInfo &ri, TaskTimes &td) {
        td.waitStart = sts_clock::now();
        functorBeginBarrier_.wait();
        td.runStart.push_back(sts_clock::now());
        functor_->run(range, ri, autoBalancing_);
        td.runEnd.push_back(sts_clock::now());
        functorEndBarrier_.markArrival();
    }
    /*
     * Notify task that we've added another thread that will run part of the
     * task (supports work splitting).
     *
     * This function must be called whenever a new thread is added that will
     * call "run," so that main thread knows how many threads to wait for.
     */
    void addThread() {
        functorEndBarrier_.addThread();
    }
    // TODO: Comments out-of-date! Combine with "run" above.
    /*! \brief
     * Run the functor. The range argument is ignored.
     *
     * This function is thread-safe and intended to be called by the thread
     * assigned to this task. Thread waits until functor is available.
     *
     * \param[in] range Range to run - ignored.
     * \return whether all functors have been assigned for this task, which
     *         is always true for a BasicTask after running its single task.
     */
    template <typename R>
    std::unique_ptr<LambdaRunner> getRunner(R range, SubTaskRunInfo &ri, TaskTimes &td) {
        int tid = Thread::getId();
        std::unique_ptr<LambdaRunner> lr = LRPool::gpool.get(Thread::getCore());
        lr->run([&,range,tid] {
            // Make sure subtasks run with the same thread id. Otherwise, calls
            // to STS inside lambda will access the wrong data structures.
            Thread::setId(tid);
            this->run(range, ri, td);
        });
        return lr;
    }
    /*! \brief
     * Searches running subtasks and attempts to steal "work" (a range of
     * iterations) from one of them. If successful, workload for that subtask
     * is reduced, and stolen iterations are assigned to passed subtask.
     *
     * Note: This function assumes the calling thread does the work and that the
     * thread is not currently "registered" for the task. That is, this function
     * increments the thread count, and main thread will wait for it to complete.
     * Deadlock occurs otherwise.
     * 
     * \param[in] subtask  subtask needing work
     * \return whether work was successfully stolen
     */
    bool stealWork(SubTask& subtask) {
        assert(subtask.getTask() == this);
        assert(subtask.getRunInfo().isRunning == false);
        if (!autoBalancing_) {
            return false;
        }

        // Find running subtask with the most remaining iterations
        std::lock_guard<std::mutex> lockAB(autoBalancingMutex_);
        size_t bestST = 0;
        // Ignore subtasks with less than 2 remaining iterations
        int64_t bestNumIters = 1;
        for (size_t st = 0; st < subtasks_.size(); st++) {
            SubTaskRunInfo ri = subtasks_[st]->getRunInfo();
            if (!ri.isRunning) {
                continue;
            }
            int64_t numIters = ri.endIter - ri.currentIter - 1;
            if (numIters > bestNumIters) {
                bestNumIters = numIters;
                bestST = st;
            }
        }

        if (bestNumIters == 1) {
            return false;
        }

        SubTaskRunInfo ri = subtasks_[bestST]->getRunInfo();
        int64_t half = ri.currentIter + (ri.endIter - ri.currentIter)/2;
        subtasks_[bestST]->setEndIter(half);
        subtask.setWorkingRange({half,ri.endIter});
        addThread();
        return true;
    }
    /*! \brief
     * Wait for all participating threads to finish
     *
     * Normally called by main thread but can be called by any thread to
     * wait for task to complete. Is thread-safe.
     */
    void wait() {
        functorEndBarrier_.wait();
    }
    // Default destructor ok
protected:
    void*    reduction_;
private:
    /*! \brief
     * Set the functor (work) to be done.
     *
     * Note: Takes ownership of passed functor
     *
     * This releases a barrier so that thread who has or will call run can
     * proceed.
     *
     * This function is not thread safe and is intended to be called only by
     * thread 0 (basic tasks cannot be nested in other tasks).
     *
     * \param[in] f Task functor
     */
    void setFunctorImpl(ITaskFunctor* f) {
        functor_.reset(f);
        functorBeginBarrier_.open();
    }
    /*! \brief
     * initialize this object for running a new functor. Nullifies any stored
     * functors and resets barriers. Intended only to be called by thread 0
     * in-between steps. Called by "restart" method.
     */
    void init() {
        functor_.reset(nullptr);
        functorBeginBarrier_.close();
        functorEndBarrier_.close(this->getNumSubtasks());
        checkPoint_ = 0;
    }
    std::string label;
    //! All subtasks of this task. One for each section of a loop. One for a basic task.
    std::vector<std::unique_ptr<SubTask>> subtasks_; //!< Subtasks to be executed by a single thread
    int numThreads_;
    // TODO: Consider whether threadTaskIds_ and runAsCoroutine_ should be in a mutex. Technically, a
    // race conditions is possible but should never happen in practice. Writing should only occur by
    // the main thread before task is executed, after which only reading should occur.
    //! Map STS thread id to an id only for this task (task ids are consecutive starting from 0)
    std::map<int, int>  threadTaskIds_;
    //! STS thread ids that should run as a coroutine (note: contains STS, non-local ids)
    std::set<int> runAsCoroutine_;
    time_point<sts_clock> functorSetTime_;
    std::unique_ptr<ITaskFunctor> functor_;      //!< The function/loop to execute
    MOBarrier functorBeginBarrier_; //!< Many-to-one barrier to sync threads at beginning of loop
    OMBarrier functorEndBarrier_; //!< One-to-many barrier to sync threads at end of loop
    std::set<std::string> nextTasks_; //!< Tasks to execute after pausing (only relevant when ran as coroutine)

    // checkPoint_ should normally only be written by the main thread (using checkpoint() method)
    // Resets to zero at beginning of each step.
    std::atomic<int> checkPoint_; //!< Allow marking of task checkpoints
    // Whether subtasks should be balanced automatically while task is running (only makes sense for loop tasks)
    bool autoBalancing_;
    // Mutex for protecting subtasks' run information when auto balancing is being used.
    std::mutex autoBalancingMutex_;
};

#endif // STS_TASK_H
