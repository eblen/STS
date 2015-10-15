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

void sts::assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num)
{
  auto it = task_map.find(task_name);
  // TODO: Need some typedefs or C++11 equivalent!
  if (it == task_map.end()) task_map.insert(std::pair< std::string, std::vector<task_part> >(task_name, std::vector<task_part>()));
  task_part tp = {iter_start, iter_end, thread_num};
  task_map[task_name].push_back(tp);
}
