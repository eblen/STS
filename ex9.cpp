/*
 * STS example code 10
 * Test auto balancing
 */
#include <chrono>
#include <cmath>

#include "sts/sts.h"

const int nsteps = 10;
const int nthreads = 10;
const int size = 1000;
float A[size] = {0};
STS *sts;

void do_something_A(int i) {
    // To see the effect of autobalancing on performance, "computation" must be
    // long enough to dwarf STS overhead. Thus, we use a sleep rather than a
    // trigonometric calculation like in other examples.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void task_f() {
    sts->parallel_for("TASK_F_0", 0, size, [=](size_t i) {do_something_A(i);});
}

void assign_threads(const int step) {
    sts->clearAssignments();
    sts->assign_run("TASK_F", 0);

    // Assumes that size/(nthreads*nthreads*nsteps) is an int
    const int inc = step * size/(nthreads*nthreads*nsteps);
    const int baseNumIters = size/nthreads;
    int t1_numer1 = 0;
    int t2_numer2 = size;
    for (int t1=0; t1<nthreads/2; t1++) {
        int t2 = nthreads - t1 - 1;

        int t1_numer2 = t1_numer1 + baseNumIters - (nthreads - 2*t1)*inc;
        int t2_numer1 = t2_numer2 - baseNumIters - (nthreads - 2*t1)*inc;
        sts->assign_loop("TASK_F_0", t1, {{t1_numer1,size},{t1_numer2,size}});
        sts->assign_loop("TASK_F_0", t2, {{t2_numer1,size},{t2_numer2,size}});

        t1_numer1 = t1_numer2;
        t2_numer2 = t2_numer1;
    }
    sts->enableTaskAutoBalancing("TASK_F_0");
}

int main(int argc, char **argv)
{
    STS::startup(nthreads);
    sts = new STS();

    for (int step=0; step<=nsteps; step++) {
        assign_threads(step);
        sts->nextStep();
        auto startTime = steady_clock::now();
        sts->run("TASK_F", task_f);
        sts->wait();
        auto endTime = steady_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        std::cout << "============ Step " << step << " ============" << std::endl;
        std::cout << "Time (ms): " << dur.count() << std::endl;
    }
    STS::shutdown();
}
