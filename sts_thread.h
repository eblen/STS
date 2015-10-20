#ifndef STS_THREAD_H
#define STS_THREAD_H

#include <atomic>
#include <cassert>

class sts;

class sts_thread
{
public:
  sts_thread(sts *s);

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

public:
  sts *scheduler;
  std::thread *cpp_thread;
  std::string task_name;
  bool is_for_loop;
  int task_start_iter;
  int task_end_iter;
  std::atomic<sts_task *> current_sts_task;

  template <typename Task>
  void set_task(std::string tn, Task t)
  {
    task_name = tn;
    is_for_loop = 0;
    current_sts_task.store(new sts_task_impl<Task>(t));
  }

  template <typename Task>
  void set_for_task(std::string tn, Task t, int start, int end)
  {
    task_name = tn;
    is_for_loop = 1;
    task_start_iter = start;
    task_end_iter = end;
    current_sts_task.store(new sts_for_loop_task_impl<Task>(t));
  }
};

#endif // STS_THREAD_H
