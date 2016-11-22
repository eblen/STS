#include <chrono>
#include <cmath>
#include <set>
#include <unistd.h>
#include "sts/sts.h"
#include "sts/thread.h"

const int nthreads = 10;
const int niters = 100000000;
float A[niters];
float B[niters];

void f() {
    STS* sts = STS::getInstance("compute");
    sts->parallel_for("TASK_F_0", 0, niters, [&](size_t i) {
        A[i] += cosf(i) + sinf(i);
        sts->yield();
    });
}

void g() {
    STS* sts = STS::getInstance("compute");
    sts->parallel_for("TASK_G_0", 0, niters, [&](size_t i) {
        B[i] += sinf(i) + cosf(i);
    });
}

void comm() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
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
    sts->assign_run("COMM", nthreads/2-1);
    sts->setHighPriority("COMM");
}

int main(int argc, char **argv)
{
  const int nsteps = 1;

  STS::startup(nthreads);
  assign_threads();

  STS* sts_compute = STS::getInstance("compute");
  for (int step=0; step<nsteps; step++)
  {
      sts_compute->nextStep();
      sts_compute->run("TASK_F", [&]{f();});
      sts_compute->run("TASK_G", [&]{g();});
      sts_compute->run("COMM", [&]{comm();});
      sts_compute->wait();
      printf("%f\n", A[niters/4] + B[niters/4]);
  }

  STS::shutdown();
}
