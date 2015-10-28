#include <iostream>
#include "sts.h"

const int niter = 60000;

void f() {
    parallel_for(0, niter, [=](size_t i) {do_someting(i);}); //I suggest that sts has a global object so one doens't need to pass it around
}

void g() {
    parallel_for(0, niter/3, [=](size_t i) {do_someting(i);});

    for(int i=0; i<niter/3; i++) { do_something(i); }

    parallel_for(0, niter/3, [=](size_t i) {do_someting(i);});
}

int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 10;
  const int TASK_F = 1; //I think this is nicer than using string constants.

  sts sched(nthreads);
  sched.assign(TASK_F, {1, 2});
  sched.assign(STS_MASTER, {0}); //or TASK_G if we use sts.run(...g) below

  for (int step=0; step<nsteps; step++)
  {
      sts.run(TASK_F, [&]{f();});
      g(); //instead this could also be sts.run and then executed by the wait
      sts.wait(); //could also be called waitall (or wait can both be with and without argument and the version without waits on all)

      //no reassigning of the taks itself so the serial gets executed still on 0 and 1

      sts.assign_steps(TASK_F, 
                       0,  //this would be the first loop inside F
                       0, 2./3, //range. Could be either the fraction of the total. But the disadvantage is that it is floating point then. Or it could be the actual iterations, but then it doesn't work well if the next step has a different number of total steps
                       1); //the thread

      sts.assign_step(STS_MASTER, 0, 0, 1./2, 0);
      sts.assign_step(STS_MASTER, 1, 0, 1./2, 0);

      sts.assign_steps(STS_MASTER, 0, 1./2, 1, 2);
      sts.assign_steps(TASK_F,     0, 2./3, 1, 2);
      sts.assign_steps(STS_MASTER, 1, 1./2, 1, 2);
  }
}
