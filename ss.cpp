#include <iostream>
#include "sts.h"

int main(int argc, char **argv)
{
  const int num_threads = 4;
  sts sched(num_threads);
  int i;
  for (i=0; i<num_threads; i++)
  {
    sched.assign_for_iter("for_loop_1", i, i, i);
    sched.assign("task0", 0);
    sched.assign("task1", 1);
    sched.assign("task2", 2);
    sched.assign("task3", 3);
  }

  auto hi_func = [&] (int i) {std::cout << "Thread " << sched.get_id() << " doing iteration " << i << std::endl;};
  auto hi_func2 = [&] () {std::cout << "Thread " << sched.get_id() << " just saying hi" << std::endl;};
  sched.parallel_for("for_loop_1", hi_func);
  sched.wait_for_all();
  sched.parallel("task0", hi_func2);
  sched.parallel("task1", hi_func2);
  sched.wait(0);
  sched.wait(1);
  sched.parallel("task2", hi_func2);
  sched.parallel("task3", hi_func2);
  sched.wait(2);
  sched.wait(3);
}
