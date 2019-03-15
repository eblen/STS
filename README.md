# STS (static thread scheduler)

### Design and Usage
The STS (static thread scheduler) library supports simple, static (as opposed to dynamic), and flexible scheduling of threads for applications that require maximum performance. STS achieves performance by minimizing overhead (static) and allowing full control over thread schedules (flexible).

Prior to any other STS calls, programs should call STS::startup(nthreads), where nthreads is the maximum number of threads used by the application. Likewise, STS::shutdown() should be called when threading is no longer needed, similar to MPI_Finalize().

The primary data type is the STS class, which implements a single STS schedule. Applications may use multiple STS schedules in a single run. In fact, between startup and shutdown, exactly one STS schedule is always active, and switching of schedules is done explicitly by specific calls (see below). Note that there is only a single thread pool, which is used by all schedules. Since only one schedule is active at a time, and since all tasks must be completed on a schedule before switching, no conflicts can arise.

At startup, a "default" schedule is active. For simple programs and portions of complex programs, this schedule may be sufficient. It automatically parallelizes loops by dividing all iterations evenly among all threads. More fine-grained control requires creating and then later switching to (activating) an STS schedule instance. The STS constructor optionally takes an std::string argument, to provide a name for the schedule. Having a name allows for later retrieving created schedules without having to explicitly store a reference to them. Often, schedules are used in a different code region than where they are created.

To switch to a schedule, call "nextStep" on the schedule instance while the default schedule is active, and call "wait" to swap back to the default schedule. The "wait" call waits for all assigned tasks to be completed. Note that STS only allows switching to/from the default schedule. No nesting of schedules is allowed. The "nextStep" call restarts the schedule and resets all threads to their first assignment, and it is not allowed to call "nextStep" while a schedule is active (between "nextStep" and "wait" calls) nor to call "wait" on an inactive schedule.

To create a schedule, use the "assign_run" and "assign_loop" methods of the STS class to assign specific threads to specific, named regions of code. See the examples provided. Note that order is important. Threads must be assigned tasks in the order in which they will execute them. There is no limit to nesting of tasks, but nesting of loop tasks inside other loop tasks is not recommended. (Thread assignment becomes confusing, and it can easily cause deadlock.)

Thread assignment has very few restrictions. One is that the thread that encounters a loop (the main thread for that loop) must also be assigned to that loop. This is necessary because the main thread needs to wait for the loop to complete before continuing. Thread assignment is otherwise very flexible, and thus it is possible, for example, for threads to switch back and forth between loops inside different run sections (see example 1).

Use the "run" and "parallel_for" methods to execute tasks. These methods input the task label and a C++ lambda, which is the work to be done. These methods simply assign the lambda to the particular task and signal to waiting threads that the work is now available.

Note that it is possible to set a schedule to use default scheduling with "setDefaultSchedule". This can be useful during development to generate a lower-bound on performance or to quickly get a schedule to work if there are problems getting assignments to work correctly. In this mode, assignments are ignored, run tasks are run by thread 0, and loops are split among all threads.

It is also possible to have tasks that are not assigned. If unassigned, run tasks will be done by thread 0, essentially ignoring the "run" function call, and unassigned "parallel_for" calls will be serialized and ran by the main thread in the containing run section. This can be useful during development. The developer can go ahead and modify code regions and loops to use STS and then later worry about actual thread assignments.

### Performance Tuning
An "auto balancing" feature is available (see example 9) to load balance loops in cases where it is difficult (or even impossible) to precompute an even distribution of loop iterations. This can be useful in cases where hardware performance varies widely (such as communication), when not all threads start work on a loop at the same time, or if iteration run times vary. Note that auto balancing works by having threads steal iterations and so works best when there are many small iterations.

Examples 5, 6, and 7 show the use of coroutines. A task set as a coroutine can use the "pause" function to pivot to another task. This functionality is important when threads need to pivot to a set of possible target tasks that are on the critical path as soon as these tasks are ready. The pause function has been highly optimized to support frequent calls ("polling"), but it is up to the application to call pause as often as needed to check the status of target tasks.

In some cases, a thread may need to oscillate between its initial task and a target task, say if the target task has multiple single-threaded regions, such as communication regions. A thread can call "pause" to return to its initial task. However, a mechanism is needed to signal to the thread when it can return to the target task. This is where "checkpointing" is helpful. When a thread pauses inside a target task, it can provide an optional checkpoint value that marks when it should return. The main thread that runs a task (including its single-threaded regions) updates the task's checkpoint value to indicate its progress. STS automatically checks these values when a thread pauses, pivoting if the requested checkpoint has been reached.

### Features
The "skip_loop" and "skip_run" methods are provided for cases where tasks may run conditionally. Note that for any task assigned to a thread, the thread must either run it or skip it. STS does not automatically skip ahead.

A simple barrier class, "MMBarrier," is provided, which will pause all threads up to a given number. Then all threads will be released. Note that this barrier only counts the number of times it is "entered" and does not work with a specific subset of threads. Note that barriers can be given names and stored inside STS, just like STS schedules. Other barriers exist (see "sts/barrier.h") that are less restrictive but probably less useful for applications.

A reduction class is provided for collecting values within a loop. These values are reduced (summed) when the loop is completed, and the result is made available inside the reduction class. See example 3. It should be fairly easy to create custom reduction classes (see sts/reduce.h) if a simple summation is not sufficient.

### Compilation
Incorporating STS into an application can be done simply by using the proper includes in the "sts" directory and compiling and linking the cpp files. C++ 11 is required and pthreads may need to be included. Here is an example on Intel KNL with the Intel compiler:

**icpc -std=c++11 -lpthread -o ex1 ex1.cpp sts/&ast;.cpp**
