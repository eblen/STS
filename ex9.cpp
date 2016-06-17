#include <cmath>
#include "sts/sts.h"

/*
 * Example of using two STS instances
 */

const int niters = 10000000;
float A[niters];
float B[niters/3];
float C[niters/3];
float D[niters/3];

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

void f(int step, std::string stsId) {
    // fprintf(stderr, "F: step=%d tid=%d\n", step, Thread::getId());

    STS::getInstance(stsId)->parallel_for("TASK_F_0", 0, niters, [=](size_t i) {do_something_A("F0", i, step);});
}

void g(int step, std::string stsId) {
    // fprintf(stderr, "G: step=%d tid=%d\n", step, Thread::getId());

    STS::getInstance(stsId)->parallel_for("TASK_G_0", 0, niters/3, [=](size_t i) {do_something_B("G0", i, step);});

    for(int i=0; i<niters/3; i++) {do_something_C("G1", i, step);}

    STS::getInstance(stsId)->parallel_for("TASK_G_1", 0, niters/3, [=](size_t i) {do_something_D("G2", i, step);});
}

void assign_threads(STS *sts) {
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

  STS *sts1 = new STS("sched1");
  for (int step=0; step<nsteps; step++)
  {
      assign_threads(sts1);
      sts1->nextStep();
      sts1->run("TASK_F", [=]{f(step, "sched1");});
      sts1->run("TASK_G", [=]{g(step, "sched1");});
      sts1->wait();
      printf("%f\n", A[niters/4] + B[niters/4] + C[niters/4] + D[niters/4]);
  }

  STS *sts2 = new STS("sched2");
  for (int step=0; step<nsteps; step++)
  {
      assign_threads(sts2);
      sts2->nextStep();
      sts2->run("TASK_F", [=]{f(step, "sched2");});
      sts2->run("TASK_G", [=]{g(step, "sched2");});
      sts2->wait();
      printf("%f\n", A[niters/4] + B[niters/4] + C[niters/4] + D[niters/4]);
  }

  STS::shutdown();
}
