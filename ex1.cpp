/*
 * STS example code 1
 * This example shows basic STS operations. Two tasks, F and G, each of which
 * contain loops, are executed asychronously. Three threads are available.
 * Thread 1 runs F, thread 2 runs G, and thread 0 acts as a helper thread,
 * 
 * Thread 0 divides its time between F and G, and loops are partitioned among
 * threads so that workload is perfectly balanced.
 */
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

    // Serial code that must be executed by thread 2 alone. During this time, thread 0 runs a
    // portion of the loop in task F.
    for(int i=0; i<niters/3; i++) {do_something_C("comm", i, step);}

    sts->parallel_for("TASK_G_1", 0, niters/3, [=](size_t i) {do_something_D("G2", i, step);});
}

// All loop iterations in all loops have the same amount of work (compute sinf)
// for easier demonstration.

// Task F consists of one large loop. Task G consists of 3 smaller loops, each
// exactly 1/3 the size of Task F's loop (so both tasks have the same amount
// of total work). Additionally, G's middle loop cannot be parallelized.

// Divide loop F0 into 6 parts. Loops G0, comm, and G1 then have of 2 parts each.
// Ideally, with three threads, each thread should run 4 parts total. STS allows
// us to do this, because we can specify how much of each loop should be executed
// by each thread, and our helper thread, thread 0, can move back and forth
// between F and G as needed,
void assign_threads() {
    sts->clearAssignments();

    sts->assign_run("TASK_F", 1);
    sts->assign_run("TASK_G", 2);

    // Thread 1 spends all of its time doing 2/3 of F0
    sts->assign_loop("TASK_F_0", 1, {0, {4,6}});

    // Thread 2 does half of G0 and G1, and all of the comm work in G.
    sts->assign_loop("TASK_G_0", 2, {0, {3,6}});
    sts->assign_loop("TASK_G_1", 2, {0, {3,6}});

    // Thread 0 does half of G0 and G1, like thread 2, but does the remaining
    // 1/3 of F0 while thread 2 is doing the comm work.
    sts->assign_loop("TASK_G_0", 0, {{3,6}, 1});
    sts->assign_loop("TASK_F_0", 0, {{4,6}, 1});
    sts->assign_loop("TASK_G_1", 0, {{3,6}, 1});
}

int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 3;

  STS::startup(nthreads);
  sts = new STS();

  for (int step=0; step<nsteps; step++)
  {
      assign_threads();
      sts->nextStep();
      sts->run("TASK_F", [=]{f(step);});
      sts->run("TASK_G", [=]{g(step);});
      sts->wait();
      printf("%f\n", A[niters/4] + B[niters/4] + C[niters/4] + D[niters/4]);
  }

  STS::shutdown();
}
