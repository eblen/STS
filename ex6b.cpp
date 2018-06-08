/*
 * STS example code 6b
 * Straightforward (no pausing) solution for ex6 for comparison
 */
#include <cmath>
#include <unistd.h>

#include "sts/sts.h"
#include "sts/thread.h"

const int nsteps = 1;
const int nthreads = 2;
const int niters = 5000000;

static float fresult = 0;
static float gresult = 0;
STS *sts;

// Fake communication function
void comm() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void task_f() {
    MMBarrier commBarrier(nthreads);
    std::vector<float> results(nthreads,0);

    sts->parallel_for("TASK_F_0", 0, nthreads, [&results,&commBarrier](int t) {
        int tid = sts->getTaskThreadId();

        for (int i=0; i<niters; i++) {
            results[tid] += sinf(i);
        }

        commBarrier.enter();
        if (t==0) {
            printf("Comm 1\n");
            // Assume this comm sends data computed in above loop and receives
            // data necessary for next loop
            comm();
        }
        commBarrier.enter();

        for (int i=0; i<niters; i++) {
            results[tid] += sinf(i);
        }

        commBarrier.enter();
        if (t==0) {
            printf("Comm 2\n");
            // Assume this comm sends data computed in above loop and receives
            // data necessary for next loop
            comm();
        }
        commBarrier.enter();

        for (int i=0; i<niters; i++) {
            results[tid] += sinf(i);
        }
    });

    for (float r : results) {
        fresult += r;
    }
    printf("f complete\n");
}

void task_g() {
    TaskReduction<float> tr = sts->createTaskReduction("TASK_G_0", float(0));
    sts->parallel_for("TASK_G_0", 0, niters, [=](int i) {
       sts->collect(cosf(i));
    }, &tr);
    gresult = tr.getResult();
    printf("g complete\n");
}

int main(int argc, char **argv)
{
  STS::startup(nthreads);
  sts = new STS();
  sts->clearAssignments();
  std::vector<int> bothThreads = {0,1};

  sts->assign_run("TASK_F", 0);
  sts->assign_loop("TASK_F_0", bothThreads);
  sts->assign_run("TASK_G", 0);
  sts->assign_loop("TASK_G_0", bothThreads);

  for (int step=0; step<nsteps; step++) {
      sts->nextStep();
      sts->run("TASK_F", task_f);
      sts->run("TASK_G", task_g);
      sts->wait();
      printf("%f %f\n", fresult, gresult);
  }
  STS::shutdown();
}
