#ifndef STS_BARRIER_H
#define STS_BARRIER_H

#include <cassert>

#include <atomic>
#include <deque>

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

class MOHyperBarrier {
public:
    // TODO: Make configurable
    static const int branchFactor = 2;

    MOHyperBarrier(int numThreads) :nthreads(numThreads) {
        for (int level = 0, levelLocks =  1; levelLocks <= nthreads / branchFactor;
                 level++,   levelLocks *= branchFactor) {
            locks.push_back(std::deque< std::atomic_bool >(levelLocks));
            // Cannot initialize in previous line due to lack of copy constructor for atomics
            for (int j=0; j<locks[level].size(); j++) {
                locks[level][j].store(true);
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
            wait_until(locks[level][tid / skip / branchFactor], false);
        }
        // Release children at lower levels
        if (tid != 0) {
            level++;
            skip /= branchFactor;
        }
        for (; level < locks.size(); level++, skip /= branchFactor) {
            locks[level][tid / skip / branchFactor].store(false);
        }
    }
private:
    const int nthreads;
    std::deque< std::deque< std::atomic_bool > > locks;
};

class OMHyperBarrier {
public:
    // TODO: Make configurable
    static const int branchFactor = 2;

    OMHyperBarrier(int numThreads) {
        for (int level = 0, levelLocks = numThreads / branchFactor; levelLocks >= 1;
                 level++,   levelLocks /= branchFactor) {
            locks.push_back(std::deque< std::atomic_int >(levelLocks));
            // Cannot initialize in previous line due to lack of copy constructor for atomics
            for (int j=0; j<locks[level].size(); j++) {
                locks[level][j].store(branchFactor-1);
            }
        }
    }
    // TODO: Consider all default methods
    OMHyperBarrier(const OMHyperBarrier &) = delete;
    void operator=(const OMHyperBarrier &) = delete;
    void enter(int tid) {
        for (int level = 0, bpow = branchFactor; level < locks.size(); level++, bpow *= branchFactor) {
            if (tid % bpow == 0) {
                wait_until(locks[level][tid / bpow], 0);
            }
            else {
                locks[level][tid / bpow]--;
                break;
            }
        }
    }
private:
    std::deque< std::deque< std::atomic_int > > locks;
};

#endif // STS_BARRIER_H
