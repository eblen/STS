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

struct sts_task
{
  std::string name;
  bool is_for_loop;
  std::vector<int> threads;
  int num_parts_running;
  std::unique_ptr<std::mutex> mutex_parts_running_count;
  std::unique_ptr<std::condition_variable> cv_task_done;

  sts_task(std::string n, bool b) :name(n), is_for_loop(b), num_parts_running(0),
           mutex_parts_running_count(new std::mutex), cv_task_done(new std::condition_variable) {}

  void add_thread(int worker_thread)
  {
    threads.push_back(worker_thread);
    if (!is_for_loop && threads.size() > 1)
    {
      std::cerr << "Error - adding more than one thread to a non-loop task." << std::endl;
    }
  }

  void add_threads(std::vector<int> worker_threads)
  {
    threads.insert(threads.end(), worker_threads.begin(), worker_threads.end());
    if (!is_for_loop && threads.size() > 1)
    {
      std::cerr << "Error - adding more than one thread to a non-loop task." << std::endl;
    }
  }
};

class sts
{
public:
  sts(int nt, int pin_offset = -1, int pin_stride = -1);
  std::thread::id get_id() const;
  void assign(std::string task_name, int thread_num);
  void assign_for_iter(std::string task_name, std::vector<int> working_threads);
  template <typename Task>
  void parallel(std::string task_name, Task task);
  template <typename Task>
  void parallel_for(std::string task_name, int num_iters, Task task);
  void wait(std::string task_name);
  void record_task_part_done(std::string task_name);

private:
  int num_threads;
  std::vector<sts_thread *> thread_pool;
  std::map< std::string, sts_task> task_map;
  sts_task &get_task(std::string task_name);
  void start_task(std::string task_name, int num_parts);
};

template <typename Task>
void sts::parallel(std::string task_name, Task task)
{
  sts_task &tn = get_task(task_name);
  assert(tn.threads.size() == 1);
  int id = tn.threads[0];

  start_task(task_name, 1);
  // If current thread is supposed to run this task, then simply run it.
  if (id == get_my_thread_id())
  {
    task();
    record_task_part_done(task_name);
  }
  else
  {
    // Cannot assign tasks to starting thread
    assert(id != 0);
    sts_thread *t = thread_pool[id];
    t->set_task(task_name, task);
  }
}

// TODO: Check that no thread has more than one part
template <typename Task>
void sts::parallel_for(std::string task_name, int num_iters, Task task)
{
  // Create task parts by dividing iterations among assigned threads
  sts_task &tn = get_task(task_name);
  assert(tn.threads.size() > 0);
  int iter_per_thread = num_iters / tn.threads.size();
  int rem_iters = num_iters % tn.threads.size();
  int start;
  int end = -1;
  int mystart = -1;
  int myend = -1;
  int i;

  start_task(task_name, tn.threads.size());
  for (i=0; i<tn.threads.size(); i++)
  {
    start = end+1;
    end = start + iter_per_thread - 1;
    if (i < rem_iters) end++;

    // If I have a part, store it and do it after starting the other threads
    if (tn.threads[i] == get_my_thread_id())
    {
      mystart = start;
      myend = end;
    }
    else
    {
      // Cannot assign tasks to starting thread
      assert(tn.threads[i] != 0);
      sts_thread *t = thread_pool[tn.threads[i]];
      t->set_for_task(task_name, task, start, end);
    }
  }

  // If I have a part, run it now
  if (mystart > -1)
  {
    for (i=mystart; i<=myend; i++) task(i);
    record_task_part_done(task_name);
  }
}

#endif // STS_H
