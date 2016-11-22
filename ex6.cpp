/*
 * STS example code 6
 * This example shows how to set a task as high priority, and later yield to
 * that task while running a different task.
 */
#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>
#include "sts/sts.h"
#include "sts/thread.h"

const int nthreads = 10;
const int niters = 10000000;
float A[niters];
float B[niters];
std::atomic<bool> commWaiting = false;

void f() {
    STS* sts = STS::getInstance("main");
    sts->parallel_for("TASK_F_multiloop", 0, niters, [&](size_t i) {
        A[i] += 1;
        // All threads yield if commWaiting is true, but only thread 0
        // executes the comm task. The other threads will simply return
        // after not finding a high-priority task on their queue.
        if (commWaiting) {
            printf("%d: high-priority communication arrived at iteration %lu\n",
                Thread::getId(),i);
            sts->yield();
            printf("%d: resuming computation\n", Thread::getId());
        }
    });
}

void g() {
    STS* sts = STS::getInstance("main");
    // The first thread in task G will signal that communication is waiting
    // halfway through its iterations.
    int commIter = niters/nthreads/2;
    sts->parallel_for("TASK_G_multiloop", 0, niters, [&](size_t i) {
        if (i == commIter) commWaiting = true;
        B[i] += 1;
    });
}

void comm() {
    commWaiting = false;
    printf("Receiving transmission...\n");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printf("Transmission received\n");
}

void assign_threads() {
    std::vector<int> f_threads(nthreads/2);
    std::vector<int> g_threads(nthreads/2);
    std::iota(f_threads.begin(), f_threads.end(), 0);
    std::iota(g_threads.begin(), g_threads.end(), nthreads/2);

    STS* sts = new STS("main");
    sts->clearAssignments();
    sts->assign_run("TASK_F", 0, f_threads);
    sts->assign_run("TASK_G", nthreads/2, g_threads);
    sts->assign_run("COMM", 0);
    // High-priority tasks are the targets for calls to yield, which can be
    // called by tasks assigned prior to the high-priority task.
    sts->setHighPriority("COMM");
}

int main(int argc, char **argv)
{
  const int nsteps = 1;

  STS::startup(nthreads);
  assign_threads();

  STS* sts_main = STS::getInstance("main");
  for (int step=0; step<nsteps; step++)
  {
      sts_main->nextStep();
      sts_main->run("TASK_F", [&]{f();});
      sts_main->run("TASK_G", [&]{g();});
      sts_main->run("COMM", [&]{comm();});
      sts_main->wait();
      printf("%f\n", A[niters/4] + B[niters/4]);
  }

  STS::shutdown();
}
