/*
 * STS example code 5
 * This example shows use of coroutines.
 */
#include <cmath>
#include <numeric>
#include <vector>

#include "sts/sts.h"
#include "sts/thread.h"

const int nsteps = 10;
const int nthreads = 10;
const int size = 100;

STS *sts;

void print_status_f(int i) {
    printf("%d: F%d\n", Thread::getId(), i);
    sts->pause();
}

void print_status_g(int i) {
    printf("%d: G%d\n", Thread::getId(), i);
    sts->pause();
}

void task_f() {
    sts->parallel_for("TASK_F_0", 0, size, [=](size_t i) {print_status_f(i);});
}

void task_g() {
    sts->parallel_for("TASK_G_0", 0, size, [=](size_t i) {print_status_g(i);});
}

int main(int argc, char **argv)
{
  STS::startup(nthreads);
  sts = new STS();
  sts->clearAssignments();

  int denom = nthreads-1;
  sts->assign_run("TASK_F", 0);
  sts->assign_loop("TASK_F_0", 0, {0,{1,denom}});
  sts->assign_run("TASK_G", 1);
  sts->assign_loop("TASK_G_0", 1, {0,{1,denom}});
  for (int t=2; t<nthreads; t++) {
      sts->assign_loop("TASK_F_0", t, {{t-1,denom},{t,denom}});
      sts->assign_loop("TASK_G_0", t, {{t-1,denom},{t,denom}});
  }

  std::vector<int> loopThreads(nthreads-2);
  std::iota(loopThreads.begin(), loopThreads.end(), 2);
  sts->setCoroutine("TASK_F_0", loopThreads, "TASK_G_0");
  // sts->setCoroutine("TASK_G_0", loopThreads, "TASK_F_0");

  for (int step=0; step<nsteps; step++) {
      sts->nextStep();
      sts->run("TASK_F", task_f);
      sts->run("TASK_G", task_g);
      sts->wait();
  }
  STS::shutdown();
}
