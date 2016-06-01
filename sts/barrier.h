#ifndef STS_BARRIER_H
#define STS_BARRIER_H

#include <cassert>
#include <cmath>

#include <atomic>
#include <deque>

#include "functions.h"

/*
 * Functions for spin waiting
 */
// TODO: Add gmx_pause.
// TODO: Consider wait_until_not variant that doesn't return a value.
/*! \brief
 * Wait until atomic variable a is set to value v
 *
 * \param[in] a   atomic variable
 * \param[in] v   value
 */
template <typename A, typename T>
void wait_until(const A &a, T v) {
    while (a.load() != v);
}
/*! \brief
 * Wait until atomic variable a is not set to value v
 *
 * \param[in] a   atomic variable
 * \param[in] v   value
 * \returns       new value of a
 */
template <typename A, typename T>
T wait_until_not(const A &a, T v) {
    T v2;
    do {
        v2 = a.load();
    } while(v2 == v);
    return v2;
}

// TODO: Asserts to check for wrong or multiple uses of barriers.

/*! \brief
 * A simple many-to-one (MO) barrier.
 */
class MOLinearBarrier {
public:
    MOLinearBarrier() :isLocked(true) {}
    /*! \brief
     * Wait on barrier. Should be called by "M" threads
     */
    void wait() {
        wait_until(isLocked, false);
    }
    /*! \brief
     * Open barrier. Should be called by "O" thread.
     */
    void open() {isLocked.store(false);}
    /*! \brief
     * Reset barrier
     */
    void close() {isLocked.store(true);}
private:
    std::atomic_bool isLocked;
};

/*! \brief
 * A simple one-to-many (OM) barrier.
 */
class OMLinearBarrier {
public:
    OMLinearBarrier() :numThreadsRemaining(0) {}
    /*! \brief
     * Register with the barrier. Should be called by "M" threads.
     */
    void markArrival() {
        numThreadsRemaining--;
    }
    /*! \brief
     * Wait on barrier. Should be called by "O" thread.
     */
    void wait() {
        wait_until(numThreadsRemaining, 0);
    }
    /*! \brief
     * Reset barrier
     */
    void close(int nthreads) {
        numThreadsRemaining.store(nthreads);
    }

private:
    std::atomic_int numThreadsRemaining;
};

// TODO: Make configurable
static const int branchFactor = 2;
static const int PADDING = 64;

class MOHyperBarrier {
public:
    MOHyperBarrier(int numThreads) :real_nthreads(numThreads),
                                    nthreads(nextpow2(std::max(branchFactor, numThreads))) {
        // TODO: Prune tree using real nthreads count
        for (int level = 0, numLocks =  1; numLocks <= nthreads / branchFactor;
                 level++,   numLocks *= branchFactor) {
            locks.push_back(std::deque< std::atomic_bool >(numLocks*PADDING));
            // Cannot initialize in previous line due to lack of copy constructor for atomics
            for (int j=0; j<numLocks; j++) {
                locks[level][j*PADDING].store(true);
            }
        }
    }
    // TODO: Consider all default methods
    MOHyperBarrier(const MOHyperBarrier &) = delete;
    void operator=(const MOHyperBarrier &) = delete;
    void enter(int tid) {
        int level = 0;
        int skip = nthreads / branchFactor;
        // Descend to first level containing a node for this thread
        for (; tid % skip != 0; level++, skip /= branchFactor);
        // Wait at that level
        if (tid != 0) {
            wait_until(locks[level][(tid / skip / branchFactor)*PADDING], false);
        }
        // Release children at lower levels
        if (tid != 0) {
            level++;
            skip /= branchFactor;
        }
        for (; level < locks.size(); level++, skip /= branchFactor) {
            locks[level][(tid / skip / branchFactor)*PADDING].store(false);
        }
    }
private:
    const int real_nthreads;
    const int nthreads;
    std::deque< std::deque< std::atomic_bool > > locks;
};

class OMHyperBarrier {
public:
    OMHyperBarrier(int numThreads) :real_nthreads(numThreads),
                                    nthreads(nextpow2(std::max(branchFactor, numThreads))) {
        for (int level = 0, skip = branchFactor; skip <= nthreads;
                 level++,   skip *= branchFactor) {
            int numLocks = std::ceil(real_nthreads / (double)skip);
            locks.push_back(std::deque< std::atomic_int >(numLocks*PADDING));
            // Cannot initialize in previous line due to lack of copy constructor for atomics
            for (int j=0; j<numLocks; j++) {
                // TODO: Avoid locks initialized to zero
                int prevSkip = skip / branchFactor;
                int remChildren = std::ceil(real_nthreads/ (double)prevSkip) - j * branchFactor - 1;
                int numChildren = std::min(branchFactor-1, remChildren);
                locks[level][j*PADDING].store(numChildren);
            }
        }
    }
    // TODO: Consider all default methods
    OMHyperBarrier(const OMHyperBarrier &) = delete;
    void operator=(const OMHyperBarrier &) = delete;
    void enter(int tid) {
        for (int level = 0, bpow = branchFactor; level < locks.size(); level++, bpow *= branchFactor) {
            if (tid % bpow == 0) {
                wait_until(locks[level][(tid / bpow)*PADDING], 0);
            }
            else {
                locks[level][(tid / bpow)*PADDING]--;
                break;
            }
        }
    }
private:
    const int real_nthreads;
    const int nthreads;
    std::deque< std::deque< std::atomic_int > > locks;
};

#endif // STS_BARRIER_H
