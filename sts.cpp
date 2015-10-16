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
  auto it = task_map.find(task_name);
  // TODO: Need some typedefs or C++11 equivalent!
  if (it == task_map.end()) task_map.insert(std::pair< std::string, std::vector<task_part> >(task_name, std::vector<task_part>()));
  else task_map[task_name].clear();
  task_part tp = {0, 0, 0, thread_num};
  task_map[task_name].push_back(tp);
}

void sts::assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num)
{
  auto it = task_map.find(task_name);
  // TODO: Need some typedefs or C++11 equivalent!
  if (it == task_map.end()) task_map.insert(std::pair< std::string, std::vector<task_part> >(task_name, std::vector<task_part>()));
  task_part tp = {1, iter_start, iter_end, thread_num};
  task_map[task_name].push_back(tp);
}

std::vector<task_part> sts::get_task_parts(std::string task_name)
{
  auto tplist_iter = task_map.find(task_name);
  if (tplist_iter == task_map.end())
  {
    std::cerr << "Error - Request to run unknown task" << std::endl;
    return std::vector<task_part>();
  }
  return (*tplist_iter).second;
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
