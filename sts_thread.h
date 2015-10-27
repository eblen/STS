#ifndef STS_THREAD_H
#define STS_THREAD_H

#include <atomic>
#include <cassert>

class sts;

class sts_thread
{
public:
  sts_thread(sts *s, int core_num = -1);

private:
  class sts_task
  {
    public:
    virtual void run() = 0;
    virtual void run(int iter) = 0;
  };

  template <typename Task>
  class sts_for_loop_task_impl : public sts_task
  {
  public:
    sts_for_loop_task_impl(Task t) :task(t) {}
    Task task;
    void run() {assert(0);}
    void run(int iter) {task(iter);}
  };

  template <typename Task>
  class sts_task_impl : public sts_task
  {
  public:
    sts_task_impl(Task t) :task(t) {}
    Task task;
    void run() {task();}
    void run(int iter) {assert(0);}
  };

  sts *scheduler;
  std::thread *cpp_thread;
  std::string task_name;
  bool is_for_loop;
  int task_start_iter;
  int task_end_iter;
  std::atomic<sts_task *> next_sts_task;
  sts_task *null_task = nullptr;

public:
  template <typename Task>
  void set_task(std::string tn, Task t)
  {
    task_name = tn;
    is_for_loop = 0;
    auto st = new sts_task_impl<Task>(t);
    while (!next_sts_task.compare_exchange_strong(null_task, st));
  }

  template <typename Task>
  void set_for_task(std::string tn, Task t, int start, int end)
  {
    task_name = tn;
    is_for_loop = 1;
    task_start_iter = start;
    task_end_iter = end;
    auto st = new sts_for_loop_task_impl<Task>(t);
    while (!next_sts_task.compare_exchange_strong(null_task, st));
  }
};

#endif // STS_THREAD_H
