#ifndef STS_H
#define STS_H

#include "sts/sts.h"

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
 * computed based on the timing from the previous step. One step contains
 * a number of scheduled tasks and a new step starts when the scheduled
 * tasks are completed. It is up to the application to decide how many tasks
 * should be scheduled together, and the scheduling step might or might not
 * be identical to the application step. A schedule can be reused or changed
 * after a step. The part of a task done by a thread is called a sub-task.
 * A simple task is always fully done by one thread and for a loop-task the
 * range done by each thread is specified. The whole design is lock free
 * and only relies on atomics.
*/

//For automatic labels one could use the file/line number such as: (strings can be avoided by using program counter instead)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

/*! \brief
 * Execute a parallel for loop
 *
 * \param[in] label    The task label (needs to match assign())
 * \param[in] start    The start index of the loop
 * \param[in] end      The end index of the loop
 * \param[in] body     The function (or lambda) to execute as loop body
 */
template<typename F>
void parallel_for(std::string l, int64_t start, int64_t end, F f)
{
    STS::getInstance()->parallel_for(l, start, end, f);
}

/*! \brief
 * Run an asynchronous function
 *
 * \param[in] label     The task label (needs to match assign())
 * \param[in] function  The function (or lambda) to execute
 */
template<typename F>
void run(std::string l, F f)
{
    STS::getInstance()->run(l, f);
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
 * \param[in] range    The range for a loop task to assign. Ignored for basic task.
 */
void assign(std::string label, int threadId, Range<Ratio> range = Range<Ratio>(1))
{
    STS::getInstance()->assign(label, threadId, range);
}

//! Notify threads to start computing the next step
void nextStep()
{
    STS::getInstance()->nextStep();
}

//! Automatically compute new schedule based on previous step timing
void reschedule()
{
    STS::getInstance()->reschedule();
}

/*! \brief
 * Set number of threads in the pool
 *
 * \param[in] n number of threads to use (including OS thread)
 */
void setNumThreads(int n)
{
    STS::getInstance()->setNumThreads(n);
}

//! Wait on all tasks to finish
void wait()
{
    STS::getInstance()->wait();
}


#endif
