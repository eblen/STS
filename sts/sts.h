#ifndef STS_STS_H
#define STS_STS_H

#include <atomic>
#include <deque>
#include <map>
#include <string>

#include "range.h"
#include "reduce.h"
#include "task.h"
#include "thread.h"

/*! \brief
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
    /*! \brief
     * Constructor
     */
    STS();
    ~STS();
    /*! \brief
     * Set number of threads in the pool
     *
     * \param[in] n number of threads to use (including OS thread)
     */
    void setNumThreads(int n);
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
    void assign(std::string label, int threadId, Range<Ratio> range = Range<Ratio>(1));
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
    //! Clear all assignments
    void clearAssignments();
    //! Notify threads to start computing the next step
    void nextStep();
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
            if (isMainThread()) {
                tasks_[getTaskId(label)].functor_.reset(new BasicTaskFunctor<F>(function));
            }
            int taskId = getTaskId(label);
            auto &thread = threads_[Thread::getId()];
            if (thread.getNextSubtask()->getTaskId()==taskId) {
                thread.processTask();
            }
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
        int taskId = 0;
        if (bUseDefaultSchedule_) {
            nextStep(); //Default schedule has only a single step and the user doesn't need to call nextStep
            assert(getTaskId("default")==taskId);
        } else {
            taskId = getTaskId(label);
        }
        auto &task = tasks_[taskId];
        auto &thread = threads_[Thread::getId()];
        if (isMainThread()) {
            task.reduction_ = red;
            task.functor_.reset(new LoopTaskFunctor<F>(body, {start, end}));
        }
        //Calling processTask implies that the thread calling parallel_for participates in the loop and executes it next in queue
        assert(thread.getNextSubtask()->getTaskId()==taskId);
        thread.processTask();
        if (isMainThread()) {
            auto startWaitTime = sts_clock::now();
            for(auto s: task.subtasks_) {
                s->wait();
            }
            task.waitTime_ = sts_clock::now() - startWaitTime;

            // TODO: A smarter reduction would take place before the above wait.
            if (task.reduction_ != nullptr) {
                auto startReductionTime = sts_clock::now();
                static_cast< TaskReduction<T> *>(task.reduction_)->reduce();
                task.reductionTime_ = sts_clock::now() - startReductionTime;
            }
        }
    }
    //! Automatically compute new schedule based on previous step timing
    void reschedule();
    //! Wait on all tasks to finish
    void wait();
    /*! \brief
     * Returns the STS instance
     *
     * \returns STS instance
     */
    static STS *getInstance() { return instance_.get(); }
    /*! \brief
     * Returns the task functor for a given task Id
     *
     * Waits on functor to be ready if the corresponding run()/parallel_for() hasn't been executed yet.
     *
     * \param[in] task Id
     * \returns task functor
     */
    ITaskFunctor *getTaskFunctor(int taskId);
    /* \brief
     * Get number of threads for current task or 0 if no current task
     *
     * \return number of threads or 0 if no current task
     */
    int getTaskNumThreads();
    /* \brief
     * Get number of threads for a given task
     *
     * \return number of threads
     */
    int getTaskNumThreads(std::string label);
    /* \brief
     * Get thread's id for its current task or -1
     *
     * \return thread task id or -1 if no current task
     */
    int getTaskThreadId();
    /* \brief
     * Load atomic step counter
     *
     * \returns step counter
     */
    int loadStepCounter();
    /* \brief
     * Wait on atomic step counter to change
     *
     * param[in] c   last step processed by thread
     */
    int waitOnStepCounter(int c);
    /* \brief
     * Is the current thread the main thread for the current task?
     *
     * \return  whether current thread is the main thread.
     */
    bool isMainThread();
private:
    // Helper function for operations that need the current task
    const Task *getCurrentTask();
    //Creates new ID for unknown label.
    //Creating IDs isn't thread safe. OK because assignments and run/parallel_for (if run without pre-assignment) are executed by master thread while other threads wait on nextStep.
    int getTaskId(std::string label);
    /*! \brief
     * Returns task label for task ID
     *
     * \param[in] id   task Id
     * \returns        task label
     */
    std::string getTaskLabel(int id) const;

    std::deque<Task>  tasks_;  //It is essential this isn't a vector (doesn't get moved when resizing). Is this ok to be a list (linear) or does it need to be a tree? A serial task isn't before a loop. It is both before and after.
    std::map<std::string,int> taskLabels_;
    std::deque<Thread> threads_;
    static std::unique_ptr<STS> instance_;
    bool bUseDefaultSchedule_ = true;
    bool bSTSDebug_ = true;
    std::atomic<int> stepCounter_;
};

#endif // STS_STS_H
