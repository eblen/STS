/*
 * STS example code 3
 * This example shows use of task reduction.
 */
#include <cmath>
#include "sts/sts.h"

const int nsteps = 2;
const int nthreads = 10;
const int size = 10;
int result = 0;

STS *sts;

void do_something_A(int i) {
    // Inside loop, threads call collect to contribute their individual values
    // Multiple calls to collect are summed together.
    sts->collect(1);
}

void task_f() {
    // Create a task reduction instance with the relevant loop task name and an
    // initial value.
    TaskReduction<int> tr = sts->createTaskReduction("TASK_F_0", 0);
    // Pass the task reduction as an additional argument to parallel_for
    sts->parallel_for("TASK_F_0", 0, size, [=](size_t i) {do_something_A(i);}, &tr);
    // Reduction occurs automatically at end of parallel_for, and the result is
    // stored in the reduction instance.
    result += tr.getResult();
}

int main(int argc, char **argv)
{
  STS::startup(nthreads);
  sts = new STS();
  sts->clearAssignments();
  sts->assign_run("TASK_F", 0);
  for (int t=0; t<nthreads; t++) sts->assign_loop("TASK_F_0", t, {{t,nthreads},{t+1,nthreads}});
  for (int step=0; step<nsteps; step++) {
      sts->nextStep();
      sts->run("TASK_F", task_f);
      sts->wait();
  }
  STS::shutdown();
  printf("Final result: %d\n", result);
  exit(0);
}
