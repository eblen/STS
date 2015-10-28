#include <iostream>
//#include "sts.h"

enum { TASK_F, TASK_G };

//start sts code. This should of course not be here but in the sts.h. This is just to show the interface and make it compile (but of course not link).
class sts {
public:
    sts(int);
    void assign_steps(int, int, double, double, int);
    template<typename F>
    void run(int, F);
    void reschedule();
    void wait();
};

template<typename F>
void parallel_for(int, int, F);

//of course this shouldn't explicit depend on the user tasks. In reallity the user tasks would be discovered.
void sts::reschedule()
{
    /* F will be on 1 and G on 2 anyhow because they are assigned round-robin but one could do it manual:
    sched.assign_task(TASK_F, 1);
    sched.assign_task(TASK_G, 2); */

    assign_steps(TASK_F, 
                     0,  //this would be the first loop inside F
                     0, 2./3, //range. Could be either the fraction of the total. But the disadvantage is that it is floating point then. Or it could be the actual iterations, but then it doesn't work well if the next step has a different number of total steps
                     1); //the thread
    
    assign_steps(TASK_G, 0, 0, 1./2, 2);
    assign_steps(TASK_G, 1, 0, 1./2, 2);
    
    assign_steps(TASK_G, 0, 1./2, 1, 0);
    assign_steps(TASK_F, 0, 2./3, 1, 0);
    assign_steps(TASK_G, 1, 1./2, 1, 0);
}
//end sts code

void do_something(int);

const int niter = 60000;

void f() {
    parallel_for(0, niter, [](size_t i) {do_something(i);});
}

void g() {
    parallel_for(0, niter/3, [](size_t i) {do_something(i);});

    for(int i=0; i<niter/3; i++) { do_something(i); }

    parallel_for(0, niter/3, [](size_t i) {do_something(i);});
}


int main(int argc, char **argv)
{
  const int nthreads = 3;
  const int nsteps = 10;

  sts sched(nthreads);

  for (int step=0; step<nsteps; step++)
  {
      sched.run(TASK_F, []{f();});
      sched.run(TASK_G, []{g();});
      sched.wait();
      sched.reschedule();
  }
}
