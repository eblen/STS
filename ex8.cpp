/*
 * STS example code 8
 * This example shows use of the STS RMOBarrier.
 */
#include <cmath>
#include <vector>

#include "sts/barrier.h"
#include "sts/sts.h"

const int nthreads = 10;

void roll_call() {
    int tid = Thread::getId();
    auto rmob = RMOBarrier::getInstance("rmob");
    auto  omb = OMBarrier::getInstance("omb");
    for (int tidCalled = 1; tidCalled < nthreads; tidCalled++) {
        if (tid==0) {
            printf("Calling %d: ", tidCalled);
            rmob->open();
            omb->wait();
            omb->close(nthreads-1);
        }
        else {
            rmob->wait(tid);
            if (tidCalled==tid) {
                printf("here!\n");
            }
            omb->markArrival();
        }
    }
}

int main(int argc, char **argv)
{
  // Initialize STS elements
  STS::startup(nthreads);
  STS* sts = new STS();
  sts->clearAssignments();
  auto rmob = new RMOBarrier(nthreads-1,"rmob");
  auto omb  = new  OMBarrier("omb");
  omb->close(nthreads-1);

  // Create thread schedule
  for (int i=0; i<nthreads; i++) {
      sts->assign_run("ROLL_CALL", i);
  }

  // Do roll call
  sts->nextStep();
  sts->run("ROLL_CALL", roll_call);
  sts->wait();

  // Shutdown STS
  STS::shutdown();
}
