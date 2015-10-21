#include <iostream>
#include "sts.h"

sts::sts(int nt) :num_threads(nt)
{
  int i;
  for (i=0; i<num_threads; i++) thread_pool.push_back(new sts_thread(this, i));
}

std::thread::id sts::get_id() const
{
  return std::this_thread::get_id();
}

void sts::assign(std::string task_name, int thread_num)
{
  task_map.emplace(std::map<std::string, sts_task>::value_type(task_name, sts_task(task_name, 0)));
  task_map.find(task_name)->second.set_thread(thread_num);
}

void sts::assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num)
{
  task_map.emplace(std::map<std::string, sts_task>::value_type(task_name, sts_task(task_name, 1)));
  task_map.find(task_name)->second.add_task_part(iter_start, iter_end, thread_num);
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

void sts::wait(std::string task_name)
{
  sts_task &t = get_task(task_name);;
  std::unique_lock<std::mutex> wait_lock(*(t.mutex_task_done));
  while (t.num_parts_running->load() > 0) t.cv_task_done->wait(wait_lock);
}

void sts::record_task_part_done(std::string task_name)
{
  sts_task &t = get_task(task_name);
  int parts_rem = t.num_parts_running->fetch_sub(1);
  assert(parts_rem > 0);
  if (parts_rem == 1) t.cv_task_done->notify_all();
}
