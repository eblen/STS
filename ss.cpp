#include <iostream>
#include "sts.h"

int main(int argc, char **argv)
{
  const int num_threads = 4;
  sts sched(num_threads);
  int i;
  for (i=0; i<num_threads; i++)
  {
    sched.assign_for_iter("for_loop_1", i, i);
  }

  auto hi_func = [&] (int i) {std::cout << "Thread " << sched.get_id() << " doing iteration " << i << std::endl;};
  sched.parallel_for("for_loop_1", 0, num_threads, hi_func);
  while(1);
}
