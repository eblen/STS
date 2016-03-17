#include <atomic>
#include <cassert>
#include <string>

#include "sts/thread.h"

/* Simple barrier implementation
 * This is a reusable barrier and so works inside loops.
 * It assumes a fixed set of exactly nthreads.
 */
class Barrier {
public:
    Barrier(int nt) :nthreads(nt), numWaitingThreads(0),
                                   numReleasedThreads(0) {
        assert(nt > 0);
    }
    void enter() {
        wait_until(numReleasedThreads, 0);
        numWaitingThreads.fetch_add(1);
        wait_until(numWaitingThreads, nthreads);
        if (numReleasedThreads.fetch_add(1) == nthreads-1) {
            numWaitingThreads.store(0);
            numReleasedThreads.store(0);
        }
    }
private:
    const int nthreads;
    std::atomic<int>  numWaitingThreads;
    std::atomic<int>  numReleasedThreads;
};
