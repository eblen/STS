#ifndef STS_STS_H
#define STS_STS_H

#include <cassert>

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <atomic>
#include <chrono>

#include "range.h"
#include "reduce.h"
#include "task.h"
#include "thread.h"

/*! \internal \brief
 * Static Thread Scheduler
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
     * No STS functions should be called before startup.
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
     * This is the most generic assign function, which does the actual work.
     * Most of the time, applications should use one of the more specific
     * assign functions.
     *
     * Note that this function can only assign one thread at a time, whereas
     * some other assign functions can assign a range of threads.
     *
     * If a range for a loop task is specified, only that section of the loop is assigned.
     * In that case it is important to assign the remaining loop out of [0,1] also to
     * some other thread. It is valid to assign multiple parts of a loop to the same thread.
     * The order of assign calls specifies in which order the thread executes the tasks.
     *
     * \param[in] label    Label of the task. Needs to match the run()/parallel_for() label
     * \param[in] ttype    Task type
     * \param[in] threadId Id of thread assigned the work
     * \param[in] range    Assigned range for a loop task. Ignored for a basic task.
     */
    enum class TaskType {BASIC, LOOP, MULTILOOP};
    void assign(std::string label, TaskType ttype, int threadId, Range<Ratio> range) {
        int id = setTask(label, ttype);
        assert(range.start>=0 && range.end<=1);
        SubTask* t = new SubTask(tasks_[id].get(), range);
        threadSubTasks_[threadId].push_back(t);
        tasks_[id]->pushSubtask(threadId, t);
    }
    /*! \brief
     * Assign a basic task to a single thread
     *
     * \param[in] label    Label of the task
     * \param[in] threadId Id of thread assigned the work
     */
    void assign_run(std::string label, int threadId) {
        assign(label, TaskType::BASIC, threadId, Range<Ratio>(1));
    }
    /*! \brief
     * Assign a basic task to a single thread along with a set of helper threads
     * to execute contained loops.
     *
     * Note that helper threads wait until the basic task completes. Loops should
     * be assigned explicitly if more fine-grained control is needed.
     *
     * \param[in] label         Label of the task
     * \param[in] threadId      Id of thread assigned the work
     * \param[in] helperThreads vector of threads that execute contained loops
     */
    void assign_run(std::string label, int threadId, const std::vector<int> &helperThreads) {
        assign_run(label, threadId);
        int nthreads = helperThreads.size();
        int numer = 0;
        int denom = nthreads;
        std::string loopTaskLabel = label + "_multiloop";
        // Main thread of a basic task is always a helper thread too. So add it
        // as a helper if not already listed.
        if (std::find(helperThreads.begin(), helperThreads.end(), threadId) == helperThreads.end()) {
            denom++;
            numer++;
            assign(loopTaskLabel, TaskType::MULTILOOP, threadId, {1, denom});
        }
        for (int i=0; i<nthreads; i++, numer++) {
            assign(loopTaskLabel, TaskType::MULTILOOP, helperThreads[i], {{numer, denom},{numer+1, denom}});
        }

        // Link the basic task with its loop task
        int parentId = getTaskId(label);
        int childId  = getTaskId(loopTaskLabel);
        BasicTask*     parentTask = dynamic_cast<BasicTask*>(tasks_[parentId].get());
        MultiLoopTask* childTask  = dynamic_cast<MultiLoopTask*>(tasks_[childId].get());
        parentTask->setMultiLoop(childTask);
    }
    /*! \brief
     * Assign a loop task to a single thread
     *
     * \param[in] label    Label of the task
     * \param[in] threadId Id of thread assigned the work
     * \param[in] range    range of loop executed by this thread
     */
    void assign_loop(std::string label, int threadId, const Range<Ratio> range) {
        assign(label, TaskType::LOOP, threadId, range);
    }
    /*! \brief
     * Assign a loop task to a vector of threads. This will split the loop
     * evenly among the given threads.
     *
     * \param[in] label     Label of the task
     * \param[in] threadIds Id of threads assigned the work
     */
    void assign_loop(std::string label, const std::vector<int> &threadIds) {
        int nthreads = threadIds.size();
        for (int i=0; i<nthreads; i++) {
            assign_loop(label, threadIds[i], {{i, nthreads},{i+1, nthreads}});
        }
    }
    //! \brief Clear all assignments
    void clearAssignments() {
        for (auto &taskList : threadSubTasks_) {
            taskList.clear();
        }
        for (std::unique_ptr<Task> &task : tasks_) {
            task->clearSubtasks();
        }
    }
    /*! \brief
     * Set this schedule to use the default schedule
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
            tasks_[getTaskId(label)]->setFunctor(new BasicTaskFunctor<F>(function));
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
            // Task object needed to do reductions
            assert(red == nullptr);
            for (int i=start; i<end; i++) {
                body(i);
            }
            return;
        } else {
            taskId = getTaskId(label);
        }
        Task* task = tasks_[taskId].get();
        assert(task != nullptr);
        task->setReduction(red);
        task->setFunctor(new LoopTaskFunctor<F>(body, {start, end}));
        SubTask* subtask = advanceToNextSubTask();
        bool isMyNextTask = (subtask != nullptr) && (subtask->getTask() == task);
        // Calling thread should either be assigned to this loop as its next task, or it should be a dummy loop.
        // Allowing the latter gives the main thread the ability to skip a single task and all of its nested loops.
        assert((start == end) || (isMyNextTask && !subtask->isDone()));
        if (isMyNextTask) {
            subtask->run();
            task->wait();
            if (red != nullptr) {
                red->reduce();
            }
            goBackToPreviousSubTask();
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
        tasks_[t]->wait();
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
     * Returns the next subtask for the given thread and updates internal
     * next subtask pointer.
     *
     * This function only accesses data specific to the calling thread, and
     * thus is thread-safe if called during a step.
     *
     * \param[in] threadId   Thread Id
     * \returns pointer to thread's next subtask
     */
    SubTask* advanceToNextSubTask() {
        int threadId = Thread::getId();
        int &st = nextSubTask_[threadId];
        for (; st < getNumSubTasks(threadId); st++) {
            if (!threadSubTasks_[threadId][st]->isDone()) {
                // return this subtask but increment pointer to next subtask
                return threadSubTasks_[threadId][st++];
            }
        }
        return nullptr;
    }
    /*! \brief
     * Go back to the last unfinished subtask for the given thread.
     *
     * This function only accesses data specific to the calling thread, and
     * thus is thread-safe if called during a step.
     *
     * This function is useful when basic tasks need to run internal loops and
     * then resume. It is purely for bookkeeping, and so no value is returned.
     *
     * \param[in] threadId Thread Id
     */
    void goBackToPreviousSubTask() {
        int threadId = Thread::getId();
        int &st = nextSubTask_[threadId];
        // Always go back at least once, regardless of finished status.
        // Multiloops, for example, may not be finished when this function is
        // called, but we still want to go back to the containing subtask.
        st -= 2;
        assert(st >= 0);
        for (; st >= 0; st--) {
            if (!threadSubTasks_[threadId][st]->isDone()) {
                // Index (st) is of the *next* subtask, not the current subtask
                st++;
                return;
            }
        }
        // Means an attempt to rewind, but no prior tasks are unfinished.
        assert(false);
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
        return tasks_[taskId]->getNumThreads();
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
        return threadSubTasks_[threadId][subTaskId]->getTask();
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
    int setTask(std::string label, TaskType ttype) {
        // TODO: Add asserts for schedule state (using isActive_ variable perhaps)
        assert(Thread::getId() == 0);
        auto it = taskLabels_.find(label);
        if (it != taskLabels_.end()) {
            return it->second;
        }
        else {
            unsigned int v = taskLabels_.size();
            assert(v==tasks_.size());
            switch (ttype) {
            case TaskType::BASIC:
                tasks_.push_back(std::unique_ptr<Task>(new BasicTask()));
                break;
            case TaskType::LOOP:
                tasks_.push_back(std::unique_ptr<Task>(new LoopTask()));
                break;
            case TaskType::MULTILOOP:
                tasks_.push_back(std::unique_ptr<Task>(new MultiLoopTask()));
                break;
            }
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
        const Task* t = getCurrentTask();
        // TODO: How to handle these user errors - calling collect outside of a
        // task or for a task without a reduction?
        if (t == nullptr) {
            return;
        }
        TaskReduction<T>* tr = (static_cast<TaskReduction<T> *>(t->getReduction()));
        if (tr == nullptr) {
            return;
        }
        tr->collect(a, ttid);
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
        for (std::unique_ptr<Task> &task: tasks_) {
            task->restart();
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
            tasks_[i]->wait();
        }

        // Wait for all threads to complete step before changing any internal state
        stepCounterBarrier_.wait();
        stepCounterBarrier_.close(getNumThreads()-1);

        isActive_ = false;
        instance_ = defaultInstance_;
    }
    // TODO: Consider using a tree of tasks if scheduling becomes unwieldy.
    // Nesting of loops inside basic tasks already makes the logic somewhat hard
    // to follow with this simple list.
    std::vector<std::unique_ptr<Task>>  tasks_;
    std::map<std::string,int> taskLabels_;
    std::vector< std::vector<SubTask *> > threadSubTasks_;
    std::vector<int> nextSubTask_; // Index of next subtask for each thread
    bool bUseDefaultSchedule_;
    // "Active" means schedule is between nextStep and wait calls.
    bool isActive_;
    // Cannot be a vector because Task moving is not allowed (occurs on resizing)
    static std::deque<Thread> threads_;
    static std::atomic<int> stepCounter_;
    static OMBarrier stepCounterBarrier_;
    static STS *defaultInstance_;
    static std::map<std::string, STS *> stsInstances_;
    static STS *instance_;
};

#endif // STS_STS_H
