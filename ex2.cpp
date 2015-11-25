#include <iostream>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <math.h>
#include "range.h"


using sts_clock = std::chrono::steady_clock;
/*! \brief
 * The part of a task done by one thread
 *
 * Contains the input to a thread what to do (taskId_, range_) and the output
 * from the thread upon completion of the sub-task (done_, timing).
 * For a loop task the range_ is the subsection done by a thread. A non-loop
 * task is always completely executed by a single thread.
 */
struct SubTask {
    /*! \brief
     * Constructor
     *
     * \param[in] taskId   The ID of the task this is part of.
     * \param[in] range    Out of a possible range from 0 to 1, the section in 
                           this part. Ignored for non-loop tasks.
     */
    SubTask(int taskId, Range<Ratio> range) : taskId_(taskId), range_(range) {
        done_.store(false, std::memory_order_release);
    }
    int taskId_;             /**< The ID of the task this is a port of */
    Range<Ratio> range_;
    std::atomic_bool done_;
    sts_clock::duration waitTime_;
    sts_clock::duration runTime_;
    void wait() const {
        while(!(done_.load(std::memory_order_acquire)));
    }
};

//Each thread corresponds to one object of this type
//Wraps std::thread and contains the task-queue
class Thread {
public:   
    Thread(int id) {
        if (id!=0) {
            thread_.reset(new std::thread([=](){id_=id; doWork();}));
        }
    }
    void processQueue(); //work function (gets either executed by std::thread  or wait(all))
    void processTask();
    SubTask const* addSubtask(int taskId, Range<Ratio> range) {
        taskQueue_.emplace_back(taskId, range);
        return &(taskQueue_.back());
    }
    void clearSubtasks() { taskQueue_.clear(); }
    SubTask const* getNextSubtask() { return &(taskQueue_[nextSubtaskId_]); }
    void resetTaskQueue() {  //should only be called by thread associated with the object
        if (id_==0) nextSubtaskId_ = 0;
        for (auto &s: taskQueue_) s.done_=false;
    }
    void join() { thread_->join(); }
    static int getId() { return id_; }
    void wait() {
        taskQueue_[taskQueue_.size()-1].wait();
    }
private:
    void doWork();

    std::deque<SubTask> taskQueue_;
    unsigned int nextSubtaskId_ = 0;
    std::unique_ptr<std::thread> thread_;
    static thread_local int id_;
};

int thread_local Thread::id_ = 0;

class ITask {
public:
    virtual void run(Range<Ratio>) = 0;
    virtual ~ITask() {};
};

template<class F>
class LoopTask : public ITask {
public:
    LoopTask<F>(F f, Range<int> r): body_(f), range_(r) {}
    void run(Range<Ratio> r) {
        Range<int> s = range_.subset(r);
        for (int i=s.start; i<s.end; i++) {
            body_(i);
        }
    }
private:
    F body_;
    Range<int> range_;
};

template<class F>
class BasicTask : public ITask {
public:
    BasicTask<F>(F f) : func_(f) {};
    void run(Range<Ratio>) {
        func_();
    }
private:
    F func_;
};

//Scoped Ptr without atomic load/store
class TaskPtr {
public:
    TaskPtr() { ptr_.store(nullptr, std::memory_order_release); }
    ~TaskPtr() { if (ptr_) delete ptr_; } // destructor isn't allowed to be called in parallel
    void store(ITask* t) { 
        ITask* o = ptr_;
        ptr_.store(t, std::memory_order_release);
        if(o) delete o; //OK because more than 1 store is not allowed
    }
    ITask* load() { return ptr_.load(std::memory_order_consume); }
private:
    std::atomic<ITask*> ptr_;
};

struct Task {
    TaskPtr task_;
    std::vector<SubTask const*> subtasks_;
    sts_clock::duration waitTime_;
};

class STS {
public:
    STS(int n) {
        instance_ = this;  //should verify that only one is ever created
        stepCounter.store(0, std::memory_order_release);
        std::vector<int> ids(n);
        std::iota(ids.begin(),ids.end(),0);
        threads_.reserve(n);
        threads_.insert(threads_.end(), ids.begin(), ids.end());
        //create default schedule
        for(int id: ids) assign("default", id, {{id,n},{id+1,n}});
    }
    ~STS() {
        stepCounter.store(-1, std::memory_order_release);
        for(unsigned int i=1;i<threads_.size();i++) {
            threads_[i].join();
        }
    }

    void assign(std::string label, int threadId, Range<Ratio> range = Range<Ratio>(1)) {
        int id = getTaskId(label);
        SubTask const* subtask = threads_.at(threadId).addSubtask(id, range);
        tasks_[id].subtasks_.push_back(subtask);
    }
    void clearAssignments() {
        for (auto &thread : threads_) {
            thread.clearSubtasks();
        }
        for (auto &task : tasks_) {
            task.subtasks_.clear();
        }
    }
    void nextStep() {
        assert(Thread::getId()==0);
        for (auto &task: tasks_) {
            task.task_.store(nullptr);
        }
        threads_[0].resetTaskQueue();
        stepCounter.store(stepCounter+1, std::memory_order_release);
    }
    template<typename F>
    void run(std::string label, F function) {
        if (bUseDefaultSchedule_) {
            function();
        } else {
            tasks_[getTaskId(label)].task_.store(new BasicTask<F>(function));
        }
    }
    template<typename F>
    void parallel_for(std::string label, int start, int end, F f) {
        int taskId = 0;
        if (bUseDefaultSchedule_) {
            nextStep();
        } else {
            taskId = getTaskId(label);
        }
        auto &task = tasks_[taskId];
        task.task_.store(new LoopTask<F>(f, {start, end}));
        auto &thread = threads_[Thread::getId()];
        assert(thread.getNextSubtask()->taskId_==taskId);
        thread.processTask(); //this implies that a task always participates in a loop and is always next in queue
        for(auto s: task.subtasks_) {
            auto startWaitTime = sts_clock::now();
            s->wait();
            task.waitTime_ = sts_clock::now() - startWaitTime;
        }
    }
    void reschedule();
    void wait() {
        if (!bUseDefaultSchedule_) {
            threads_[0].processQueue();
            for(unsigned int i=1;i<threads_.size();i++) {
                threads_[i].wait();
            }
            if (bSTSDebug_) {
                std::cerr << "Times for step " << stepCounter.load() << std::endl;
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
    static STS *getInstance() { return instance_; } //should this auto-create it if it isn't created by the user?
    ITask *getTask(int taskId) { 
        ITask *t;
        while(!(t=tasks_[taskId].task_.load()));
        return t;
    }
private:
    int getTaskId(std::string label) { //Creates new ID for unknown label. Creating IDs isn't thread safe. OK because assignments and run/parallel_for (if run without preassigment) are executed by master thread.
        auto it = taskLabels_.find(label);
        if (it != taskLabels_.end()) {
            return it->second;
        } else {
            assert(Thread::getId()==0);
            unsigned int v = taskLabels_.size();
            assert(v==tasks_.size());
            assert(v==taskIds_.size());
            tasks_.resize(v+1);
            taskLabels_[label] = v;
            taskIds_.push_back(label);
            return v;
        }
    }

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
public:
    std::atomic<int> stepCounter;
};

STS *STS::instance_ = nullptr;

//for automatic labels: (strings can be avoided by using program counter instead) 
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

void Thread::doWork() {
    STS *sts = STS::getInstance();
    for (int i=0; ; i++) {
        int c;
        while ((c=sts->stepCounter.load(std::memory_order_acquire))==i);
        resetTaskQueue();
        if (c<0) break;
        processQueue();
    }
}

void Thread::processQueue() { 
    int s = taskQueue_.size();
    while(nextSubtaskId_<s) {
        processTask();
    }
    nextSubtaskId_=0;
}

void Thread::processTask() {
    auto& subtask = taskQueue_[nextSubtaskId_++];
    auto startWaitTime = sts_clock::now();
    ITask *task = STS::getInstance()->getTask(subtask.taskId_);
    auto startTaskTime = sts_clock::now();
    subtask.waitTime_ = startTaskTime - startWaitTime;
    task->run(subtask.range_);
    subtask.runTime_ = sts_clock::now() - startTaskTime;
    subtask.done_.store(true, std::memory_order_release); //add store
}

//of course this shouldn't explicitly depend on the user tasks. In reality the user tasks would be discovered.
void STS::reschedule()
{
    clearAssignments();
    bUseDefaultSchedule_ = false;

    assign("TASK_F", 1);
    assign("TASK_G", 2); 

    assign("TASK_F_0", 1, {0, {2,3}}); 
    
    assign("TASK_G_0", 2, {0, {1,2}});
    assign("TASK_G_1", 2, {0, {1,2}});
    
    assign("TASK_G_0", 0, {{1,2}, 1});
    assign("TASK_F_0", 0, {{2,3}, 1});
    assign("TASK_G_1", 0, {{1,2}, 1});

    nextStep();
}
//end sts code. start example

const int niters = 1000000;
float A[niters];
float B[niters/3];
float C[niters/3];
float D[niters/3];

void do_something_A(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    A[i] = sinf(i);
}

void do_something_B(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    B[i] = sinf(i);
}

void do_something_C(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    C[i] = sinf(i);
}

void do_something_D(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    D[i] = sinf(i);
}

void f(int step) {
    // fprintf(stderr, "F: step=%d tid=%d\n", step, Thread::getId());

    parallel_for("TASK_F_0", 0, niters, [=](size_t i) {do_something_A("F0", i, step);});
}

void g(int step) {
    // fprintf(stderr, "G: step=%d tid=%d\n", step, Thread::getId());

    parallel_for("TASK_G_0", 0, niters/3, [=](size_t i) {do_something_B("G0", i, step);});

    for(int i=0; i<niters/3; i++) {do_something_C("G1", i, step);}

    parallel_for("TASK_G_1", 0, niters/3, [=](size_t i) {do_something_D("G2", i, step);});
}

int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 3;

  STS sched(nthreads);

  for (int step=0; step<nsteps; step++)
  {
      /*
      if(step==2) 
          sched.reschedule(); //can be done every step if desired
      if(step==3) 
          sched.nextStep();
      */
      sched.reschedule();
      sched.run("TASK_F", [=]{f(step);});
      sched.run("TASK_G", [=]{g(step);});
      sched.wait();
      printf("%f\n", A[niters/4] + B[niters/4] + C[niters/4] + D[niters/4]);
  }
}
