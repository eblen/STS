#include <atomic>
#include <cassert>
#include <string>

#include "sts/thread.h"

/* Simple barrier implementation
 * This is a reusable barrier and so works inside loops.
 * It assumes a fixed set of exactly nthreads.
 */
template<int nthreads>
class Barrier {
public:
    Barrier() :numWaitingThreads(0), numReleasedThreads(0) {}
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
    std::atomic<int>  numWaitingThreads;
    std::atomic<int>  numReleasedThreads;
};
