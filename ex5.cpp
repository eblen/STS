#include <cmath>
#include "sts.h"

const int nsteps = 2;
const int nthreads = 10;
const int size = 10;
int result = 0;

void do_something_A(int i) {
    collect(1);
}

void task_f() {
    TaskReduction<int> tr = createTaskReduction("TASK_F_0", 0);
    parallel_for("TASK_F_0", 0, size, [=](size_t i) {do_something_A(i);}, &tr);
    result += tr.getResult();
}

int main(int argc, char **argv)
{
  setNumThreads(nthreads);
  clearAssignments();
  assign("TASK_F", 0);
  for (int t=0; t<nthreads; t++) assign("TASK_F_0", t, {{t,nthreads},{t+1,nthreads}});
  for (int step=0; step<nsteps; step++) {
      nextStep();
      run("TASK_F", task_f);
      wait();
  }
  printf("Final result: %d\n", result);
  exit(0);
}
