#include <set>
#include "sts/sts.h"
#include "sts/thread.h"

const int nthreads = 12;
const int niters = 1000;
float A[niters];
float B[niters];

void f() {
    STS* sts = STS::getInstance("compute");
    sts->parallel_for("TASK_F_0", 0, niters, [&](size_t i) {
        A[i] =  1;
    });
    sts->parallel_for("TASK_F_1", 0, niters, [&](size_t i) {
        A[i] += 1;
    });
    sts->parallel_for("TASK_F_2", 0, niters, [&](size_t i) {
        A[i] += 1;
    });
    sts->parallel_for("TASK_F_3", 0, niters, [&](size_t i) {
        A[i] += 1;
    });
}

void g() {
    STS* sts = STS::getInstance("compute");
    sts->parallel_for("TASK_G_0", 0, niters, [&](size_t i) {
        B[i] =  1;
    });
    sts->parallel_for("TASK_G_1", 0, niters, [&](size_t i) {
        B[i] += 1;
    });
    sts->parallel_for("TASK_G_2", 0, niters, [&](size_t i) {
        B[i] += 1;
    });
    sts->parallel_for("TASK_G_3", 0, niters, [&](size_t i) {
        B[i] += 1;
    });
}

void h() {
    STS::getInstance("reduce")->parallel_for("TASK_H_0", 0, niters, [&](size_t i) {
        A[i] += B[i];
    });
}

void assign_threads() {
    std::set<int> f_threads;
    std::set<int> g_threads;
    std::set<int> h_threads;
    for (int i=0; i<nthreads/2; i++) {
        f_threads.insert(i);
        h_threads.insert(i);
    }
    for (int i=nthreads/2; i<nthreads; i++) {
        g_threads.insert(i);
        h_threads.insert(i);
    }

    STS* sts = new STS("compute");
    sts->clearAssignments();
    sts->assign_run("TASK_F", 0, f_threads);
    sts->assign_run("TASK_G", nthreads/2, g_threads);


    sts = new STS("reduce");
    sts->clearAssignments();
    sts->assign_run("TASK_H", 0, h_threads);
}

int main(int argc, char **argv)
{
  const int nsteps = 3;

  STS::startup(nthreads);
  assign_threads();

  STS* sts_compute = STS::getInstance("compute");
  STS* sts_reduce  = STS::getInstance("reduce");
  for (int step=0; step<nsteps; step++)
  {
      sts_compute->nextStep();
      sts_compute->run("TASK_F", [&]{f();});
      sts_compute->run("TASK_G", [&]{g();});
      sts_compute->wait();
      sts_reduce->nextStep();
      sts_reduce->run("TASK_H",  [&]{h();});
      sts_reduce->wait();
      printf("%f\n", A[niters/4]);
  }

  STS::shutdown();
}
