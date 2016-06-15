#include <cmath>
#include "sts/sts.h"

const int niters = 10000000;
float A[niters];
float B[niters/3];
float C[niters/3];
float D[niters/3];

STS *sts;

void do_something_A(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    A[i] = sinf(i);
}

void do_something_B(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    B[i] = sinf(i);
}

void do_something_C(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    C[i] = sinf(i);
}

void do_something_D(const char* s, int i, int step) {
    // fprintf(stderr, "%s: i=%d step=%d tid=%d\n", s, i, step, Thread::getId());
    D[i] = sinf(i);
}

void f(int step) {
    // fprintf(stderr, "F: step=%d tid=%d\n", step, Thread::getId());

    sts->parallel_for("TASK_F_0", 0, niters, [=](size_t i) {do_something_A("F0", i, step);});
}

void g(int step) {
    // fprintf(stderr, "G: step=%d tid=%d\n", step, Thread::getId());

    sts->parallel_for("TASK_G_0", 0, niters/3, [=](size_t i) {do_something_B("G0", i, step);});

    for(int i=0; i<niters/3; i++) {do_something_C("G1", i, step);}

    sts->parallel_for("TASK_G_1", 0, niters/3, [=](size_t i) {do_something_D("G2", i, step);});
}

void assign_threads() {
    sts->clearAssignments();

    sts->assign("TASK_F", 1);
    sts->assign("TASK_G", 2);

    sts->assign("TASK_F_0", 1, {0, {2,3}});

    sts->assign("TASK_G_0", 2, {0, {1,2}});
    sts->assign("TASK_G_1", 2, {0, {1,2}});

    sts->assign("TASK_G_0", 0, {{1,2}, 1});
    sts->assign("TASK_F_0", 0, {{2,3}, 1});
    sts->assign("TASK_G_1", 0, {{1,2}, 1});
}

int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 3;

  STS::startup(nthreads);
  sts = new STS();

  for (int step=0; step<nsteps; step++)
  {
      /*
      if(step==2) 
          sts->reschedule(); //can be done every step if desired
      if(step==3) 
          sts->nextStep();
      */
      assign_threads();
      sts->nextStep();
      sts->run("TASK_F", [=]{f(step);});
      sts->run("TASK_G", [=]{g(step);});
      sts->wait();
      printf("%f\n", A[niters/4] + B[niters/4] + C[niters/4] + D[niters/4]);
  }

  STS::shutdown();
}
