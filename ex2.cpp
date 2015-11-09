#include <deque>
#include <thread>
#include <memory>
#include <vector>
#include "range.h"

enum { TASK_F, TASK_G, TASK_F_0, TASK_G_0, TASK_G_1 };

//sts code. This should of course not be here but in the sts.h. This is just to show the interface and make it compile (but of course not link).

template< class T, class... Args >
std::unique_ptr<T> make_unique( Args&&... args )
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

struct SubTask {
    SubTask(int taskId, Range<Ratio> range) : taskId_(taskId), range_(range) {}
    int taskId_;
    Range<Ratio> range_;
};

class Thread {
public:   
    Thread(bool isMaster = false) {
        if (!isMaster) {
            thread_.reset(new std::thread([&](){doWork();}));
        }
    }
    void doWork(); //work function (gets either executed by std::thread  or wait(all))
    void addSubtask(int taskId, Range<Ratio> range) {
        taskQueue_.emplace_back(SubTask(taskId, range));
    }

private:
    std::deque<SubTask> taskQueue_;
    std::unique_ptr<std::thread> thread_;
};

class ITask {
public:
    virtual void run(Range<Ratio>);
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


class STS {
public:
    STS(int n) {
        instance_ = this;  //should verify that not 2 are created
        threads_.emplace_back(true);
        threads_.resize(n);
    }
    void assign(int taskId, int threadId, Range<Ratio> range = Range<Ratio>(1)) {
        threads_.at(threadId).addSubtask(taskId, range);
    }
    template<typename F>
    void run(int taskId, F function) {
        reserve_tasks(taskId+1);
        tasks_[taskId] = make_unique<BasicTask<F>>(function);
    }
    template<typename F>
    void parallel_for(int start, int end, F f) {
        //TODO
    }

    void reschedule();
    void wait(); //TODO

    static STS *getInstance() { return instance_; } //should this auto-create it if isn't created by the user?
    ITask *getTask(int taskId) { return tasks_[taskId].get(); }
private:
    void reserve_tasks(unsigned int n) {
        if (tasks_.size() > n)
            tasks_.resize(n);
    }       

    std::deque<std::unique_ptr<ITask>>  tasks_;  //Is this ok to be a vector (linear) or does it need to be a tree. A serial tasks isn't before a loop. It is both before and after. 
    std::vector<Thread> threads_;
    static STS *instance_;
};

template<typename F>
void parallel_for(int start, int end, F f) {
    STS::getInstance()->parallel_for(start, end, f);
}

void Thread::doWork() { //work function (gets either executed by std::thread  or wait(all))
    //How does it end? Do subtask get reused? How does it restart the queue?
    //Simplest so far: assumes all are assigned before. No restarting
    for (auto subtask: taskQueue_) {
        ITask *task = STS::getInstance()->getTask(subtask.taskId_);
        task->run(subtask.range_);
        //execute lambda
    }
}

//of course this shouldn't explicit depend on the user tasks. In reallity the user tasks would be discovered.
void STS::reschedule()
{
    // F will be on 1 and G on 2 anyhow because they are assigned round-robin but one could do it manual:
    assign(TASK_F, 1);
    assign(TASK_G, 2); 

    assign(TASK_F_0, 1,
           Range<Ratio>(0, {2,3})); //range. Could be either the fraction of the total. But the disadvantage is that it is floating point then. Or it could be the actual iterations, but then it doesn't work well if the next step has a different number of total steps
    
    assign(TASK_G_0, 2, {0, {1,2}});
    assign(TASK_G_1, 2, {0, {1,2}});
    
    assign(TASK_G_0, 0, {{1,2}, 1});
    assign(TASK_F_0, 0, {{2,3}, 1});
    assign(TASK_G_1, 0, {{1,2}, 1});
}
//end sts code

void do_something(int);

const int niter = 60000;

void f() {
    parallel_for(0, niter, [](size_t i) {do_something(i);});
}

void g() {
    parallel_for(0, niter/3, [](size_t i) {do_something(i);});

    for(int i=0; i<niter/3; i++) { do_something(i); }

    parallel_for(0, niter/3, [](size_t i) {do_something(i);});
}


int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 10;

  STS sched(nthreads);

  for (int step=0; step<nsteps; step++)
  {
      sched.run(TASK_F, []{f();});
      sched.run(TASK_G, []{g();});
      sched.wait();
      sched.reschedule();
  }
}
