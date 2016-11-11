#ifndef STS_STS_H
#define STS_STS_H

#include <cassert>

#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <atomic>
#include <chrono>

#include "range.h"
#include "reduce.h"
#include "task.h"
#include "thread.h"

/* Overall design:
 * The framework can execute simple tasks (via "run") and execute loops in
 * parallel (via "parallel_for"). It supports two run modi: either with an
 * explicit schedule or with a default schedule. With the default schedule
 * tasks are run in serial and only loop level parallelism is used. This is
 * useful if either the tasks are not yet known or only simple parallelism is
 * needed. With an explicit schedule one can specify which task runs on which
 * thread and in which order (based on the order of task assignment). Loops
 * can be split among the threads using ratios (e.g. thread 0 does 1/3 of
 * the loop while thread 1 does the remaining 2/3). The idea is that this
 * schedule is either computed by the user of the framework using "assign"
 * or automatically computed by the framework using "reschedule." (Automatic
 * scheduling is not yet implemented.) Timing data is recorded for each task
 * so that adjustments can be made (or not) after each "step." One "step"
 * contains a number of scheduled tasks and a new step starts when "nextStep"
 * is called. Normally, a step will be one iteration of a main loop, like a
 * time step in MD, but this is of course not required. The part of a task
 * done by a thread is called a sub-task. A simple task is always fully
 * done by one thread and for a loop-task the range done by each thread is
 * specified. The whole design is lock free and only relies on atomics.
 */

/*! \internal \brief
 * Static task scheduler
 *
 * Allows running an asynchronous function with run() and execute loops in parallel
 * with parallel_for(). The default schedule only uses loop level parallelism and
 * executes run() functions synchronously. A schedule with task level parallelism
 * is created automatically by calling reschedule() or manual by calling assign().
 * After the queue of tasks in one schedule are completed, one can do one of
 * three things for the next step:
 * a) call nextStep() and reuse the schedule
 * b) call reschedule() to let the scheduler automatically compute a new schedule
 * c) call clearAssignments(), assign(), and nextStep() to manual specify schedule
 */
class STS {
public:
    /*! \brief Schedule Id
     *
     * Can be used to retrieve schedules with getInstance() rather than storing
     * them in the application code.
     */
    const std::string id;
    /*! \brief
     * Startup STS and set the number of threads.
     * No STS functions should be called before Startup.
     */
    static void startup(size_t numThreads) {
        assert(numThreads > 0);
        assert(threads_.size() == 0); // Do not allow multiple calls

        // Barrier must be initialized before creating threads
        // First time the barrier is used, each non-main thread enters it twice
        stepCounterBarrier_.close(2*(numThreads-1));

        for (size_t id = 0; id < numThreads; id++) {
            threads_.emplace_back(id); //create threads
        }

        // Create the default STS instance, which uses a default schedule.
        // This schedule provides a quick way to parallelize loops.
        defaultInstance_ = new STS("default");
        defaultInstance_->setDefaultSchedule();
        instance_ = defaultInstance_;
    }
    /*! \brief
     * Stops all threads.
     * No STS functions should be called after Shutdown.
     */
    static void shutdown() {
        assert(instance_ == defaultInstance_);
        //-1 notifies threads to finish
        stepCounter_.store(-1, std::memory_order_release);
        for (unsigned int i=1;i<getNumThreads();i++) {
            threads_[i].join();
        }
        delete defaultInstance_;
    }
    /*! \brief
     * Constructs a new STS schedule
     *
     * \param[in] name  optional name for schedule
     */
    STS(std::string name = "") :id(name), bUseDefaultSchedule_(false), isActive_(false) {
        int n = getNumThreads();
        assert(n > 0);
        threadSubTasks_.resize(n);
        if (!id.empty()) {
            stsInstances_[id] = this;
        }
        nextSubTask_.resize(n, 0);
    }
    ~STS() {
        for (auto& taskList : threadSubTasks_) {
            for (SubTask *t : taskList) {
                delete t;
            }
        }
        if (!id.empty()) {
            stsInstances_.erase(id);
        }
    }
    /*! \brief
     * Get number of threads in the pool
     */
    static int getNumThreads() {
        auto n = threads_.size();
        assert(n > 0);
        return n;
    }
    /*! \brief
     * Assign task to a thread
     *
     * If a range for a loop task is specified, only that section of the loop is assigned.
     * In that case it is important to assign the remaining loop out of [0,1] also to
     * some other thread. It is valid to assign multiple parts of a loop to the same thread.
     * The order of assign calls specifies in which order the thread executes the tasks.
     *
     * \param[in] label    The label of the task. Needs to match the run()/parallel_for() label
     * \param[in] threadId The Id of the thread to assign to
     * \param[in] range    The range for a loop task to assing. Ignored for basic task.
     */
    void assign(std::string label, int threadId, Task::Type ttype,
            Range<Ratio> range, const std::set<int> &uaTaskThreads) {
        int id = setTask(label, ttype, uaTaskThreads);
        assert(range.start>=0 && range.end<=1);
        SubTask *t = new SubTask(tasks_[id], range);
        threadSubTasks_[threadId].push_back(t);
        tasks_[id].pushSubtask(threadId, t);
    }
    void assign_run(std::string label, int threadId, const std::set<int> &uaTaskThreads = {}) {
        assign(label, threadId, Task::RUN, Range<Ratio>(1), uaTaskThreads);
    }
    void assign_loop(std::string label, int threadId, Range<Ratio> range) {
        assign(label, threadId, Task::LOOP, range, {});
    }
    //! \brief Clear all assignments
    void clearAssignments() {
        for (auto &taskList : threadSubTasks_) {
            taskList.clear();
        }
        for (auto &task : tasks_) {
            task.clearSubtasks();
        }
    }
    /*! \brief
     * Set the default schedule
     *
     * All previous assignments are cleared. Loops are divided evenly among all threads and
     * non-loop tasks simply run on the invoking thread,
     */
    void setDefaultSchedule() {
        bUseDefaultSchedule_ = true;
        clearAssignments();
        int numThreads = getNumThreads();
        for (int id = 0; id < numThreads; id++) {
            assign_loop("default", id, {{id, numThreads}, {id + 1, numThreads}});
        }
    }
    /*! \brief
     * Run an asynchronous function
     *
     * \param[in] label     The task label (needs to match assign())
     * \param[in] function  The function (or lambda) to execute
     */
    template<typename F>
    void run(std::string label, F function) {
        if (!bUseDefaultSchedule_) {
            assert(this == instance_);
            // Cannot invoke an instance that is inactive
            // (nextStep has not been called)
            assert(instance_->isActive_ == true);
        }
        else {
            // Instances with a default schedule can be run at any time except
            // in the middle of an active schedule.
            assert(instance_->isActive_ == false);
        }
        if (!isTaskAssigned(label) || bUseDefaultSchedule_) {
            function();
        } else {
            tasks_[getTaskId(label)].functor_ = new BasicTaskFunctor<F>(function);
            tasks_[getTaskId(label)].functorBeginBarrier_.open();
        }
    }
    //! Notify threads to start computing the next step
    void nextStep() {
        if (!bUseDefaultSchedule_) {
            nextStepInternal();
        }
    }
    /*! \brief
     * Execute a parallel for loop
     *
     * \param[in] label    The task label (needs to match assign())
     * \param[in] start    The start index of the loop
     * \param[in] end      The end index of the loop
     * \param[in] body     The function (or lambda) to execute as loop body
     * \param[in] red      Optional reduction
     */
    template<typename F, typename T=int>
    void parallel_for(std::string label, int64_t start, int64_t end, F body, TaskReduction<T> *red = nullptr) {
        if (!bUseDefaultSchedule_) {
            assert(this == instance_);
            // Cannot invoke an instance that is inactive
            // (nextStep has not been called)
            assert(instance_->isActive_ == true);
        }
        else {
            // Instances with a default schedule can be run at any time except
            // in the middle of an active schedule.
            assert(instance_->isActive_ == false);
        }
        int taskId = -1;
        if (bUseDefaultSchedule_) {
            assert(isTaskAssigned("default"));
            taskId = getTaskId("default");
            assert(taskId == 0); // Default schedule should have only the "default" task with id 0.
            nextStepInternal();  // Default schedule has only a single step and the user doesn't need to call nextStep
        } else if (!isTaskAssigned(label)) {
            for (int i=start; i<end; i++) {
                body(i);
            }
            if (red != nullptr) {
                red->reduce();
            }
            return;
        } else {
            taskId = getTaskId(label);
        }
        auto &task = tasks_[taskId];
        task.reduction_ = red;
        task.functor_ = new LoopTaskFunctor<F>(body, {start, end});
        task.functorBeginBarrier_.open();
        int tid = Thread::getId();
        int nst = nextSubTask_[tid];
        SubTask *subtask = nullptr;
        if (nst < getNumSubTasks(tid)) {
            subtask = threadSubTasks_[tid][nst];
        }
        bool isMyNextTask = (subtask != nullptr) && (&subtask->getTask() == &task);
        // Calling thread should either be assigned to this loop as its next task, or it should be a dummy loop.
        // Allowing the latter gives the main thread the ability to skip a single task and all of its nested loops.
        assert((start == end) || isMyNextTask);
        if (isMyNextTask) {
            assert(threads_[tid].processTask()); // false return value contradicts previous assertion
            auto startWaitTime = sts_clock::now();
            task.functorEndBarrier_.wait();
            task.waitTime_ = sts_clock::now() - startWaitTime;
            // TODO: A smarter reduction would take place before the above wait.
            if (task.reduction_ != nullptr) {
                auto startReductionTime = sts_clock::now();
                static_cast< TaskReduction<T> *>(task.reduction_)->reduce();
                task.reductionTime_ = sts_clock::now() - startReductionTime;
            }
        }
        // User does not need to call wait for default scheduling
        if (bUseDefaultSchedule_) {
            waitInternal();
        }
    }
    /*! \brief
     * Skip the given run task.
     *
     * This is useful when an assigned task should not run under certain conditions.
     *
     * \param[in] label  task label
     */
    void skip_run(std::string label) {
        run(label, []{});
    }
    /*! \brief
     * Skip the given loop task.
     *
     * This is useful when an assigned task should not run under certain conditions.
     *
     * \param[in] label  task label
     */
    void skip_loop(std::string label) {
        // Return i to avoid compiler warnings about an unused parameter
        parallel_for(label, 0, 0, [](int i){return i;});
    }
    //! Automatically compute new schedule based on previous step timing
    void reschedule() {
        // not yet available
    }
    //! Wait for specific task to finish
    void waitForTask(std::string label) {
        assert(isTaskAssigned(label));
        int t = getTaskId(label);
        tasks_[t].functorEndBarrier_.wait();
    }
    //! Wait on all tasks to finish
    void wait() {
        if (!bUseDefaultSchedule_) {
            waitInternal();
        }
    }
    /*! \brief
     * Returns STS instance for a given id or default instance if not found
     *
     * \param[in] id  STS instance Id
     * \returns STS instance
     */
    static STS *getInstance(std::string id) {
        auto entry = stsInstances_.find(id);
        if (entry == stsInstances_.end()) {
            return defaultInstance_;
        }
        else {
            return entry->second;
        }
    }
    /*! \brief
     * Returns current STS instance
     *
     * WARNING: meant only for internal use. Applications should use
     * "getInstance" for better error checking and clarity when using
     * multiple STS instances.
     *
     * \returns current STS instance
     */
    static STS *getCurrentInstance() {
        return instance_;
    }
    /*! \brief
     * Advances to and returns the next subtask for the given thread
     *
     * \param[in] threadId   Thread Id
     * \returns pointer to thread's next subtask
     */
    SubTask *advanceToNextSubTask(int threadId) {
        int st = nextSubTask_[threadId]++;
        if (st >= getNumSubTasks(threadId)) {
            return nullptr;
        }
        else {
            return threadSubTasks_[threadId][st];
        }
    }
    /*! \brief
     * Get number of subtasks assigned to a thread
     *
     * \param[in] threadId  Thread Id
     */
    int getNumSubTasks(int threadId) const {
        return threadSubTasks_[threadId].size();
    }
    /*! \brief
     * Get number of threads for current task or 0 if no current task
     *
     * \return number of threads or 0 if no current task
     */
    int getTaskNumThreads() {
        const Task *t = getCurrentTask();
        if (t == nullptr) {
            return 0;
        }
        return t->getNumThreads();
    }
    /*! \brief
     * Get number of threads for a given task
     *
     * \param[in] label  task label
     * \return number of threads
     */
    int getTaskNumThreads(std::string label) {
        assert(isTaskAssigned(label));
        int taskId = getTaskId(label);
        return tasks_[taskId].getNumThreads();
    }
    /*! \brief
     * Get thread's id for its current task or -1
     *
     * \return thread task id or -1 if no current task
     */
    int getTaskThreadId() {
        const Task *t = getCurrentTask();
        if (t == nullptr) {
            return -1;
        }
        int ttid = t->getThreadId(Thread::getId());
        // Would mean that thread is currently running a task it was never assigned.
        assert(ttid > -1);
        return ttid;
    }
    /*! \brief
     * Load atomic step counter
     *
     * \returns step counter
     */
    static int loadStepCounter() { return stepCounter_.load(std::memory_order_acquire); }
    /*! \brief
     * Wait on atomic step counter to change
     *
     * param[in] c   last step processed by thread
     */
    static int waitOnStepCounter(int c) {
        stepCounterBarrier_.markArrival();
        return wait_until_not(stepCounter_, c);
    }

    /* Task reduction functions */

    /*! \brief
     * Create a TaskReduction object
     *
     * \param[in] taskName  Name of task to which reduction applies
     * \param[in] init      Initial value
     */
    template<typename T>
    TaskReduction<T> createTaskReduction(std::string taskName, T init) {
        int numThreads = getTaskNumThreads(taskName);
        return TaskReduction<T>(init, numThreads);
    }
    /*! \brief
     * Collect a value for a task's reduction. Must be called within a task.
     *
     * \param[in] a Value to be collected
     */
    template<typename T>
    void collect(T a) {
        collect(a, getTaskThreadId());
    }
private:
    const Task *getCurrentTask() {
        int threadId = Thread::getId();
        int subTaskId = nextSubTask_[threadId]-1;
        if (subTaskId < 0 || subTaskId >= getNumSubTasks(threadId)) {
            return nullptr;
        }
        return &threadSubTasks_[threadId][subTaskId]->getTask();
    }
    bool isTaskAssigned(std::string label) const {
        return (taskLabels_.find(label) != taskLabels_.end());
    }
    /* \brief
     * Get task id for task label
     *
     * Undefined behavior if task doesn't exist.
     *
     * \param[in] task label
     * \return task id
     */
    int getTaskId(std::string label) {
        auto it = taskLabels_.find(label);
        assert(it != taskLabels_.end());
        return it->second;
    }
    /* \brief
     * Sets values for a task, creating a new task object if it doesn't exist.
     *
     * Creating tasks isn't thread safe, so this should only be called when
     * doing thread assignments between schedule runs.
     * \param[in] label for task
     * \return task id
     */
    int setTask(std::string label, Task::Type ttype, const std::set<int> &uaTaskThreads) {
        // TODO: Add asserts for schedule state (using isActive_ variable perhaps)
        assert(Thread::getId() == 0);
        auto it = taskLabels_.find(label);
        if (it != taskLabels_.end()) {
            // Do not allow changing of task type
            assert(ttype == tasks_[it->second].getType());
            return it->second;
        }
        else {
            unsigned int v = taskLabels_.size();
            assert(v==tasks_.size());
            tasks_.emplace_back(ttype, uaTaskThreads);
            taskLabels_[label] = v;
            return v;
        }
    }
    /* \brief
     * Collect the given value for the current task for later reduction
     *
     * \param[in] value to collect
     */
    template<typename T>
    void collect(T a, int ttid) {
        const Task *t = getCurrentTask();
        // TODO: This is a user error - calling collect outside of a task.
        // Currently, we simply ignore the call. How should it be handled?
        if (t == nullptr) {
            return;
        }
        (static_cast<TaskReduction<T> *>(t->reduction_))->collect(a, ttid);
    }
    //! Notify threads to start computing the next step
    void nextStepInternal() {
        assert(Thread::getId()==0);
        assert(instance_->bUseDefaultSchedule_ == true);
        // Allow multiple calls, but ignore if schedule is active.
        if (isActive_) {
            assert(instance_ == this);
            return;
        }
        // Cannot swap out an active schedule (call wait first)
        assert(instance_->isActive_ == false);
        instance_ = this;
        isActive_ = true;
        for (auto &task: tasks_) {
            task.functor_ = nullptr;
            task.functorBeginBarrier_.close();
            task.functorEndBarrier_.close(task.getNumThreads());
        }
        nextSubTask_.assign(getNumThreads(), 0);

        // Increment counter only
        stepCounter_.fetch_add(1, std::memory_order_release);
    }
    //! Wait on all tasks to finish
    void waitInternal() {
        assert(Thread::getId()==0);
        assert(this == instance_);
        // Allow multiple calls, but ignore if schedule is not active.
        if (!isActive_) {
            return;
        }
        threads_[0].processQueue(); //Before waiting the OS thread executes its queue
        for(unsigned int i=1;i<tasks_.size();i++) {
            tasks_[i].functorEndBarrier_.wait();
        }

        // Wait for all threads to complete step before changing any internal state
        stepCounterBarrier_.wait();
        stepCounterBarrier_.close(getNumThreads()-1);

        isActive_ = false;
        instance_ = defaultInstance_;
    }
    std::deque<Task>  tasks_;  //It is essential this isn't a vector (doesn't get moved when resizing). Is this ok to be a list (linear) or does it need to be a tree? A serial task isn't before a loop. It is both before and after.
    std::map<std::string,int> taskLabels_;
    std::vector< std::vector<SubTask *> > threadSubTasks_;
    std::vector<int> nextSubTask_; // Index of next subtask for each thread
    bool bUseDefaultSchedule_;
    // "Active" means schedule is between nextStep and wait calls.
    bool isActive_;
    static std::deque<Thread> threads_;
    static std::atomic<int> stepCounter_;
    static OMBarrier stepCounterBarrier_;
    static STS *defaultInstance_;
    static std::map<std::string, STS *> stsInstances_;
    static STS *instance_;
};

#endif // STS_STS_H
