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
  std::cout << "IDK how to assign" << std::endl;
}

void sts::assign_for_iter(std::string task_name, int iter_num, int thread_num)
{
  std::cout << "IDK how to assign" << std::endl;
}
