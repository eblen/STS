#ifndef STS_H
#define STS_H

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <cassert>
#include <map>
#include <mutex>
#include <condition_variable>
#include "sts_thread.h"

typedef struct task_part_struct
{
  int start;
  int end;
  int id;
} task_part;

struct sts_task
{
  std::string name;
  bool is_for_loop;
  std::unique_ptr< std::atomic<int> > num_parts_running;
  std::vector<task_part> task_parts;
  std::unique_ptr<std::mutex> mutex_task_done;
  std::unique_ptr<std::condition_variable> cv_task_done;

  sts_task(std::string n, bool b) :name(n), is_for_loop(b), num_parts_running(new std::atomic<int>(0)),
                                   mutex_task_done(new std::mutex), cv_task_done(new std::condition_variable) {}

  void set_thread(int thread_num)
  {
    if (is_for_loop)
    {
      std::cerr << "Error - cannot set thread for looping tasks." << std::endl;
    }
    else
    {
      task_parts.clear();
      task_part tp = {0, 0, thread_num};
      task_parts.push_back(tp);
    }
  }

  void add_task_part(int iter_start, int iter_end, int thread_num)
  {
    if (!is_for_loop)
    {
      std::cerr << "Error - cannot add a task part to a non-loop task." << std::endl;
    }
    else
    {
      task_part tp = {iter_start, iter_end, thread_num};
      task_parts.push_back(tp);
    }
  } 
};

class sts
{
public:
  sts(int nt, int pin_offset = 0, int pin_stride = 1);
  std::thread::id get_id() const;
  void assign(std::string task_name, int thread_num);
  void assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num);
  template <typename Task>
  void parallel(std::string task_name, Task task);
  template <typename Task>
  void parallel_for(std::string task_name, Task task);
  void wait(std::string task_name);
  void record_task_part_done(std::string task_name);

private:
  int num_threads;
  std::vector<sts_thread *> thread_pool;
  std::map< std::string, sts_task> task_map;
  sts_task &get_task(std::string task_name);
};

template <typename Task>
void sts::parallel(std::string task_name, Task task)
{
  sts_task &tn = get_task(task_name);
  assert(tn.task_parts.size() == 1);
  tn.num_parts_running->store(1);
  task_part tp = tn.task_parts[0];
  sts_thread *t = thread_pool[tp.id];
  t->set_task(task_name, task);
}

template <typename Task>
void sts::parallel_for(std::string task_name, Task task)
{
  sts_task &tn = get_task(task_name);
  tn.num_parts_running->store(tn.task_parts.size());
  int i;
  for (i=0; i<tn.task_parts.size(); i++)
  {
    task_part tp = tn.task_parts[i];
    sts_thread *t = thread_pool[tp.id];
    t->set_for_task(task_name, task, tp.start, tp.end);
  }
}

#endif // STS_H
