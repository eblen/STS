/*
 * STS example code 7
 * "Hello World" using alternating coroutines
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
    printf("H");
    sts->pause();
    printf("l");
    sts->pause();
    printf("o");
    sts->pause();
    printf("W");
    sts->pause();
    printf("r");
    sts->pause();
    printf("d");
}

void task_g() {
    printf("e");
    sts->pause();
    printf("l");
    sts->pause();
    printf(" ");
    sts->pause();
    printf("o");
    sts->pause();
    printf("l");
    sts->pause();
    printf("\n");
}

int main(int argc, char **argv)
{
  STS::startup(1);
  sts = new STS();
  sts->clearAssignments();

  sts->assign_run("TASK_F", 0);
  sts->assign_run("TASK_G", 0);
  std::vector<int> t0 = {0};
  sts->setCoroutine("TASK_F", t0, "TASK_G");
  sts->setCoroutine("TASK_G", t0, "TASK_F");

  sts->nextStep();
  sts->run("TASK_F", task_f);
  sts->run("TASK_G", task_g);
  sts->wait();
  STS::shutdown();
}
