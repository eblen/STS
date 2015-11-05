#include <iostream>
#include "sts.h"
#include "sts_thread.h"

sts::sts(int nt, int pin_offset, int pin_stride) :num_threads(nt)
{
  if (pin_offset != -1) set_affinity(pin_offset);
  thread_pool.push_back(nullptr);

  int i;
  for (i=1; i<num_threads; i++)
  {
    if (pin_offset != -1) thread_pool.push_back(new sts_thread(this, i, i*pin_stride + pin_offset));
    else thread_pool.push_back(new sts_thread(this, i));
  }
}

std::thread::id sts::get_id() const
{
  return std::this_thread::get_id();
}

void sts::assign(std::string task_name, int worker_thread)
{
  task_map.emplace(std::map<std::string, sts_task>::value_type(task_name, sts_task(task_name, 0)));
  task_map.find(task_name)->second.add_thread(worker_thread);
}

void sts::assign_for_iter(std::string task_name, std::vector<int> worker_threads)
{
  task_map.emplace(std::map<std::string, sts_task>::value_type(task_name, sts_task(task_name, 1)));
  task_map.find(task_name)->second.add_threads(worker_threads);
}

sts_task &sts::get_task(std::string task_name)
{
  auto stlist_iter = task_map.find(task_name);
  if (stlist_iter == task_map.end())
  {
    std::cerr << "Error - Request for unknown task" << std::endl;
  }
  return (*stlist_iter).second;
}

void sts::start_task(std::string task_name, int num_parts)
{
  sts_task &t = get_task(task_name);
  std::unique_lock<std::mutex> pr_lock(*(t.mutex_parts_running_count));
  t.num_parts_running = num_parts;
}

void sts::wait(std::string task_name)
{
  sts_task &t = get_task(task_name);
  std::unique_lock<std::mutex> pr_lock(*(t.mutex_parts_running_count));
  while (t.num_parts_running > 0) {t.cv_task_done->wait(pr_lock);}
}

void sts::record_task_part_done(std::string task_name)
{
  sts_task &t = get_task(task_name);
  std::unique_lock<std::mutex> pr_lock(*(t.mutex_parts_running_count));
  t.num_parts_running--;
  assert(t.num_parts_running >= 0);
  if (t.num_parts_running == 0) t.cv_task_done->notify_all();
}
