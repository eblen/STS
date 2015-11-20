#ifndef STS_H
#define STS_H

#include <cassert>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "range.h"

#if __GNUC__ == 4 && __GNUC_MINOR__ <= 7
#define thread_local __thread
#endif

using sts_clock = std::chrono::steady_clock;

/* Overall design:
 * The framework can execute simple tasks (via "run") and execute loops in
 * parallel (via "parallel_for"). It supports two run modi: either with an
 * explicit schedule or with a default schedule. With the default schedule
 * tasks are run in serial and only loop level parallelism is used. This
 * is useful if either the tasks are not yet known or only simple parallelism
 * is needed. With an explicit schedule one can specify which task runs on
 * which thread and in which order. For loops one can specify which part
 * of a loop is done by each participating thread. The idea is that this
 * schedule is either provided by the user of the framework or automatically
 * computed based on the timing from the privious step. One step contains
 * a number of scheduled tasks and a new step starts when the scheduled
 * tasks are completed. It is up to the application to decide how many tasks
 * should be scheduled together and the scheduling step, might or might not
 * be identical to the application step. A schedule can be reused or changed
 * after a step. The part of a task done by a thread is called a sub-task.
 * A simple task is always fully done by one thread and for a loop-task the
 * range done by each thread is specified. The whole design is lock free
 * and only relies on atomics.
*/

/*! \brief
 * The part of a task done by one thread
 *
 * Contains the input to a thread what to do (taskId_, range_) and the output
 * from the thread upon completion of the sub-task (done_, timing).
 * For a loop task the range_ is the subsection done by a thread. A basic
 * task is always completely executed by a single thread.
 */
class SubTask {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] taskId   The ID of the task this is part of.
     * \param[in] range    Out of a possible range from 0 to 1, the section in
                           this part. Ignored for basic tasks.
     */
    SubTask(int taskId, Range<Ratio> range) : taskId_(taskId), range_(range) {
        setDone(false);
    }
    //! Wait on sub-task to complete
    void wait() const
    {
        while(!(done_.load(std::memory_order_acquire))); //TODO: add gmx_pause
    }
    /*! \brief
     * Set done status.
     *
     * \param[in] done   value
     */
    void setDone(bool done)
    {
        done_.store(done, std::memory_order_release);
    }
    int taskId_;                   /**< The ID of the task this is a part of */
    Range<Ratio> range_;           /**< Range (out of [0,1]) of loop part */
    sts_clock::duration waitTime_; /**< Time spent until task was ready */
    sts_clock::duration runTime_;  /**< Time spent sexecuting sub-task  */
private:
    std::atomic_bool done_;  /**< Sub-task is done */
};

/*! \brief
 * Every thread has one associated object of this type
 *
 * Both the OS created (master-) thread and all threads created by STS
 * have an object of this type. The later ones contain a std::thread
 * in thread_. Each object contains the queue of subtasks executed
 * by the respective thread during one step.
 */
class Thread {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] id   Id given to this thread. 0 is the OS thread.
     */
    Thread(int id) {
        if (id!=0) {
            thread_.reset(new std::thread([=](){id_=id; doWork();}));
        }
    }
    /*! \brief
     * Execute the whole queue of subtaks
     *
     * Gets executed for the OS thread by STS::wait and for STS created
     * threads from Thread::doWork
     */
    void processQueue();
    //! Execute the next subtask in the queue
    void processTask();
    /*! \brief
     * Add a subtask to the end of the queue
     *
     * \param[in] taskID  The ID of the task to add
     * \param[in] range   The range of the loop to be done by this thread
     *                    Ignored for basic tasks.
     * \returns           Pointer to sub-task added
    */
    SubTask const* addSubtask(int taskId, Range<Ratio> range) {
        taskQueue_.emplace_back(taskId, range);
        return &(taskQueue_.back());
    }
    /*! \brief
     * Clear all sub-tasks in the queue.
     *
     * Is called by clearAssignments to prepare for rescheduling tasks. */
    void clearSubtasks() { taskQueue_.clear(); }
    /*! \brief
     * Return sub-task next in queue
     *
     * \returns      Pointer to sub-task
     */
    SubTask const* getNextSubtask() { return &(taskQueue_[nextSubtaskId_]); }
    /*! \brief
     * Resets queue in prepartion for the next step
     *
     * Does not delete queue. @see clearSubtasks
     */
    void resetTaskQueue() {
        if (!thread_) nextSubtaskId_ = 0;
        for (auto &s: taskQueue_) s.setDone(false);
    }
    //! Wait for thread to finish
    void join() { if (thread_) thread_->join(); }
    /*! \brief
     * Return thread Id
     *
     * Note: This is a static method. Id returned depends on the thread executing
     * not the object. Only use Thread::getID() not t.getId() to avoid confusion.
     *
     * \returns thread id
     */
    static int getId() { return id_; }
    //! Wait for thread to finish all tasks in queue (=step)
    void wait() {
        for(auto &task: taskQueue_) task.wait(); //If we have a task-graph it is sufficient to wait on last parent task. Without graph we need to make sure all are done.
    }
private:
    void doWork(); //function executed by worker threads

    std::deque<SubTask> taskQueue_;
    unsigned int nextSubtaskId_ = 0;
    std::unique_ptr<std::thread> thread_;
    static thread_local int id_;
};

//! Interface of the executable function of a task
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

//! Loop task functor
template<class F>
class LoopTaskFunctor : public ITaskFunctor {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] f    lambda of loop body
     * \param[in] r    range of loop
     */
    LoopTaskFunctor<F>(F f, Range<int> r): body_(f), range_(r) {}
    void run(Range<Ratio> r) {
        Range<int> s = range_.subset(r); //compute sub-range of this execution
        for (int i=s.start; i<s.end; i++) {
            body_(i);
        }
    }
private:
    F body_;
    Range<int> range_;
};

//! Basic (non-loop) task functor
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

//! Atomic scoped_ptr like smart pointer
template<class T>
class AtomicPtr {
public:
    AtomicPtr() { ptr_.store(nullptr, std::memory_order_release); }
    ~AtomicPtr() {
        T* p = get();
        if(p) delete p;
    }
    /*! \brief
     * Deletes the previous object and stores new pointer
     *
     * \param[in] t   pointer to store
     */
    void reset(T* t) {
        T* o = ptr_.exchange(t, std::memory_order_release);
        if(o) delete o;
    }
    /*! \brief
     * Returns the stored pointer
     *
     * \returns pointer
     */
    T* get() { return ptr_.load(std::memory_order_consume); }
private:
    std::atomic<T*> ptr_;
};

/*! \brief
 * A task to be executed
 *
 * Can either be a function or loop. Depending on the schedule is
 * executed synchronous or asynchronous. Functions are always
 * executed by a single threads. Loops are executed, depending on
 * the schedule, in serial or in parallel.
 */
struct Task {
    AtomicPtr<ITaskFunctor> functor_;      //!< The function/loop to execute
    //! All subtasks of this task. One for each section of a loop. One for a basic task.
    std::vector<SubTask const*> subtasks_;
    //!< The waiting time in the implied barrier at the end of a loop. Zero for basic task.
    sts_clock::duration waitTime_;
};

/*! \brief
 * Static task scheduler
 *
 * Allows to run asynchronous function with run() and execute loops in parallel
 * with parallel_for(). The default schedule only uses loop level parallism and
 * executes run() functions synchronous. A schedule with task level parallelism
 * is created automatically by calling reschedule() or manual by calling assign().
 * After the queue of tasks in one schedule are completed, one can do one of
 * three things for the next step:
 * a) call nextStep() and reuse the schedule
 * b) call reschedule() to let the scheduler automatically compute a new schedule
 * c) call clearAssignments(), assign(), and nextStep() to manual specify schedule
 */
class STS {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] n Number of threads to use (including OS thread)
     */
    STS(int n) {
        assert(!instance_);
        instance_ = this;
        stepCounter_.store(0, std::memory_order_release);
        threads_.reserve(n);
        for (int id = 0; id < n; id++) {
            threads_.emplace_back(id); //create threads
            //create default schedule
            assign("default", id, { { id,n },{ id + 1,n } });
        }
    }
    ~STS() {
        //-1 notifies threads to finish
        stepCounter_.store(-1, std::memory_order_release);
        for(unsigned int i=1;i<threads_.size();i++) {
            threads_[i].join();
        }
    }
    /*! \brief
     * Assign task to a thread
     *
     * If a range for a loop task is specified only that section of the loop is assigned.
     * In that case it is important to assign the remaining loop out of [0,1] also to
     * some thread. It is valid to assign multiple parts of a loop to the same thread.
     * The order of assign calls specifies in which order the thread executes the tasks.
     *
     * \param[in] label    The label of the task. Needs to match the run()/parallel_for() label
     * \param[in] threadId The Id of the thread to assign to
     * \param[in] range    The range for a loop task to assing. Ignored for basic task.
     */
    void assign(std::string label, int threadId, Range<Ratio> range = Range<Ratio>(1)) {
        int id = getTaskId(label);
        SubTask const* subtask = threads_.at(threadId).addSubtask(id, range);
        tasks_[id].subtasks_.push_back(subtask);
    }
    //! Clear all assignments
    void clearAssignments() {
        for (auto &thread : threads_) {
            thread.clearSubtasks();
        }
        for (auto &task : tasks_) {
            task.subtasks_.clear();
        }
    }
    //! Notify threads to start computing the next step
    void nextStep() {
        assert(Thread::getId()==0);
        for (auto &task: tasks_) {
            task.functor_.reset(nullptr);
        }
        threads_[0].resetTaskQueue();
        stepCounter_.fetch_add(1, std::memory_order_release);
    }
    /*! \brief
     * Run an asynchronous function
     *
     * \param[in] label     The task label (needs to match assign())
     * \param[in] function  The function (or lambda) to execute
     */
    template<typename F>
    void run(std::string label, F function) {
        if (bUseDefaultSchedule_) {
            function();
        } else {
            tasks_[getTaskId(label)].functor_.reset(new BasicTaskFunctor<F>(function));
        }
    }
    /*! \brief
     * Execute a parallel for loop
     *
     * \param[in] label    The task label (needs to match assign())
     * \param[in] start    The start index of the loop
     * \param[in] end      The end index of the loop
     * \param[in] body     The function (or lambda) to execute as loop body
     */
    template<typename F>
    void parallel_for(std::string label, int start, int end, F body) {
        int taskId = 0;
        if (bUseDefaultSchedule_) {
            nextStep(); //Default schedule has only a single step and the user doesn't need to call nextStep
        } else {
            taskId = getTaskId(label);
        }
        auto &task = tasks_[taskId];
        task.functor_.reset(new LoopTaskFunctor<F>(body, {start, end}));
        auto &thread = threads_[Thread::getId()];
        //Calling processTask implies that the thread calling parallel_for participates in the loop and executes it next in queue
        assert(thread.getNextSubtask()->taskId_==taskId);
        thread.processTask();
        for(auto s: task.subtasks_) {
            auto startWaitTime = sts_clock::now();
            s->wait();
            task.waitTime_ = sts_clock::now() - startWaitTime;
        }
    }
    //! Automatic compute new schedule based on previous step timing
    void reschedule();
    //! Wait on all tasks to finish
    void wait() {
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
                        std::cerr << getTaskLabel(st->taskId_) << " " << wtime << " " << rtime << std::endl;
                    }
                    if (t.subtasks_.size() > 1) {
                        auto ltwtime = std::chrono::duration_cast<std::chrono::microseconds>(t.waitTime_).count();
                        std::cerr << "Wait for task to complete " << ltwtime << std::endl;
                    }
                }
            }
        }
    }
    /*! \brief
     * Returns the STS instance
     *
     * \returns STS instance
     */
    static STS *getInstance() { return instance_; } //should this auto-create it if it isn't created by the user?
    /*! \brief
     * Returns the task functor for a given task Id
     *
     * Waits on functor to be ready if the correspong run()/parallel_for() hasn't been executed yet.
     *
     * \param[in] task Id
     * \returns task functor
     */
    ITaskFunctor *getTaskFunctor(int taskId) {
        ITaskFunctor *t;
        while(!(t=tasks_[taskId].functor_.get()));
        return t;
    }
    /* \brief
     * Load atomic step counter
     *
     * \returns step counter
     */
    int loadStepCounter() { return stepCounter_.load(std::memory_order_acquire); }
private:
    //Creates new ID for unknown label.
    //Creating IDs isn't thread safe. OK because assignments and run/parallel_for (if run without preassigment) are executed by master thread while other threads wait on nextStep.
    int getTaskId(std::string label) {
        auto it = taskLabels_.find(label);
        if (it != taskLabels_.end()) {
            return it->second;
        } else {
            assert(Thread::getId()==0); //creating thread should only be done by master thread
            unsigned int v = taskLabels_.size();
            assert(v==tasks_.size());
            assert(v==taskIds_.size());
            tasks_.resize(v+1);
            taskLabels_[label] = v;
            taskIds_.push_back(label);
            return v;
        }
    }
    /*! \brief
     * Returns task label for task ID
     *
     * \param[in] id   task Id
     * \returns        task label
     */
    std::string getTaskLabel(int id) const {
        assert(-1 < id < taskIds_.size());
        return taskIds_[id];
    }
    std::deque<Task>  tasks_;  //It is essential this isn't a vector (doesn't get moved when resizing). Is this ok to be a list (linear) or does it need to be a tree? A serial task isn't before a loop. It is both before and after.
    std::map<std::string,int> taskLabels_;
    std::vector<std::string> taskIds_;
    std::vector<Thread> threads_;
    static STS *instance_;
    bool bUseDefaultSchedule_ = true;
    bool bSTSDebug_ = true;
    std::atomic<int> stepCounter_;
};

//For automatic labels one could use the file/line number such as: (strings can be avoided by using program counter instead)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

template<typename F>
void parallel_for(std::string l, int start, int end, F f) {
    STS::getInstance()->parallel_for(l, start, end, f);
}

template<typename F>
void run(std::string l, F f) {
    STS::getInstance()->run(l, f);
}

#endif
