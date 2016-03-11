#include <cmath>
#include "sts.h"

const int nsteps = 10;
const int nthreads = 4;
const int niters = 100;
float A[niters];

void do_something_A(int i) {
    A[i] = sinf(i);
    int j = (i + niters / 4) % niters;
    A[i] += A[j];
}

void task_f() {
    parallel_for("TASK_F_0", 0, niters, [=](size_t i) {do_something_A(i);});
}

int main(int argc, char **argv)
{
  setNumThreads(nthreads);
  clearAssignments();
  assign("TASK_F", 0);
  for (int t=0; t<nthreads; t++) assign("TASK_F_0", t, {{t,nthreads},{t+1,nthreads}});
  for (int step=0; step<nsteps; step++) {
      printf("CP0\n");
      nextStep();
      printf("CP1\n");
      run("TASK_F", task_f);
      printf("CP2\n");
      wait();
      printf("CP3\n");
  }
  int num_samples = 10;
  for (int i=0; i<num_samples; i++) printf("%f\n", A[i*(niters/num_samples)]);
}
