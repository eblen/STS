#include <iostream>
#include "sts.h"

sts::sts(int nt) :num_threads(nt)
{
  int i;
  for (i=0; i<num_threads; i++) thread_pool.push_back(new sts_thread());
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

std::vector<task_part> sts::get_task_parts(std::string task_name)
{
  auto stlist_iter = task_map.find(task_name);
  if (stlist_iter == task_map.end())
  {
    std::cerr << "Error - Request to run unknown task" << std::endl;
    return std::vector<task_part>();
  }
  return (*stlist_iter).second.task_parts;
}

void sts::wait(int thread_id)
{
  thread_pool[thread_id]->wait(thread_id);
}

void sts::wait_for_all()
{
  int i;
  for (i=0; i<num_threads; i++) wait(i);
}
