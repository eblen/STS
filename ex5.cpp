/*
 * STS example code 5
 * This example shows how to assign all loops in a run section to a set of
 * helper threads, without having to assign every loop.
 */

#include <cmath>
#include "sts/sts.h"

#include <numeric>
#include <algorithm>

const int nsteps = 2;
const int nthreads = 10;
const int size = 10;
int result = 0;

STS *sts;

void do_something_A(int i) {
    sts->collect(1);
}

void do_something_B(int i) {
    sts->collect(1);
}

void do_something_C(int i) {
    sts->collect(1);
}

void task_f() {
    // The label for unassigned loops is "_multiloop" appended to the containing
    // task's name. All of the loops share this label. Loop iterations are
    // divided evenly among the helper threads, which also always includes the
    // thread assigned to the run task.
    TaskReduction<int> tr = sts->createTaskReduction("TASK_F_multiloop", 0);
    sts->parallel_for("TASK_F_multiloop", 0, size, [=](size_t i) {do_something_A(i);}, &tr);
    sts->parallel_for("TASK_F_multiloop", 0, size, [=](size_t i) {do_something_B(i);}, &tr);
    sts->parallel_for("TASK_F_multiloop", 0, size, [=](size_t i) {do_something_C(i);}, &tr);
    result += tr.getResult();
}

int main(int argc, char **argv)
{
  STS::startup(nthreads);
  sts = new STS();
  sts->clearAssignments();
  // Assign helper threads by passing to "assign_run" an additional argument, a
  // vector of helper threads.
  // Note that the thread assigned to the containing run task will also be a
  // helper, regardless of whether it is in the vector of helper threads.
  // Currently, STS does not support both assigned and unassigned loops in the
  // same run task. Such code will hang, because helper threads wait excusively
  // for unassigned loops while the run task is running.
  std::vector<int> threadList(nthreads);
  std::iota(threadList.begin(), threadList.end(), 0);
  sts->assign_run("TASK_F", 0, threadList);
  for (int step=0; step<nsteps; step++) {
      sts->nextStep();
      sts->run("TASK_F", task_f);
      sts->wait();
  }
  STS::shutdown();
  printf("Final result: %d\n", result);
  exit(0);
}
