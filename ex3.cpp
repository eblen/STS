#include <cmath>

#include "barrier.h"
#include "sts.h"

const int nsteps = 10;
const int nthreads = 10;
const int size = 100;
float A[size] = {0};
float B[size] = {0};

void do_something_A(int i) {
    static Barrier b(nthreads);
    A[i] = 1;
    int j = (i + size / nthreads) % size;
    b.enter();
    B[i] += A[i] + A[j];
}

void task_f() {
    parallel_for("TASK_F_0", 0, size, [=](size_t i) {do_something_A(i);});
}

int main(int argc, char **argv)
{
  setNumThreads(nthreads);
  clearAssignments();
  assign("TASK_F", 0);
  for (int t=0; t<nthreads; t++) assign_loop("TASK_F_0", t, {{t,nthreads},{t+1,nthreads}});
  for (int step=0; step<nsteps; step++) {
      nextStep();
      run("TASK_F", task_f);
      wait();
  }
  int num_samples = 4;
  for (int i=0; i<num_samples; i++) printf("%f\n", B[i*(size/num_samples)]);
}
