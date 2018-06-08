/*
 * STS example code 6
 * This example shows use of checkpointing.
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
    OMBarrier commBarrier;
    commBarrier.close(nthreads-1);
    std::vector<float> results(nthreads,0);

    sts->parallel_for("TASK_F_0", 0, nthreads, [&results,&commBarrier](int t) {
        int tid = sts->getTaskThreadId();

        for (int i=0; i<niters; i++) {
            results[tid] += sinf(i);
        }

        if (t==0) {
            commBarrier.wait();
            commBarrier.close(nthreads-1);
            printf("Comm 1\n");
            // Assume this comm sends data computed in above loop and receives
            // data necessary for next loop
            comm();
            sts->setCheckPoint(1);
        }
        else {
            commBarrier.markArrival();
            sts->pause(1);
        }

        for (int i=0; i<niters; i++) {
            results[tid] += sinf(i);
        }

        if (t==0) {
            commBarrier.wait();
            printf("Comm 2\n");
            // Assume this comm sends data computed in above loop and receives
            // data necessary for next loop
            comm();
            sts->setCheckPoint(2);
        }
        else {
            commBarrier.markArrival();
            sts->pause(2);
        }

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
    for (int i=0; i<niters; i++) {
       gresult += cosf(i);
       if (sts->pause()) printf("g paused at %d\n", i);
    }
    printf("g complete\n");
}

int main(int argc, char **argv)
{
  STS::startup(nthreads);
  sts = new STS();
  sts->clearAssignments();
  std::vector<int> bothThreads = {0,1};

  sts->assign_run("TASK_F", 0);
  sts->assign_run("TASK_G", 1);
  sts->assign_loop("TASK_F_0", bothThreads);

  sts->setCoroutine("TASK_G", bothThreads, "TASK_F_0");
  sts->setCoroutine("TASK_F_0", bothThreads, "TASK_G");

  for (int step=0; step<nsteps; step++) {
      sts->nextStep();
      sts->run("TASK_F", task_f);
      sts->run("TASK_G", task_g);
      sts->wait();
      printf("%f %f\n", fresult, gresult);
  }
  STS::shutdown();
}
