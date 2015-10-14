#include <iostream>
#include "sts.h"

int main(int argc, char **argv)
{
  sts sched(60);
  int i;
  for (i=0; i<60; i++)
  {
    sched.assign_for_iter("for_loop_1", i, i);
  }

  auto hi_func = [&] (int i) {std::cout << "Thread " << sched.get_id() << " doing iteration " << i << std::endl;};
  sched.parallel_for("for_loop_1", 0, 60, hi_func);
}
