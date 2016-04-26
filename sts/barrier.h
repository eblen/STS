#ifndef STS_BARRIER_H
#define STS_BARRIER_H

#include <cassert>

#include <atomic>

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
class MOBarrier {
public:
    MOBarrier() :isLocked(true) {}
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
class OMBarrier {
public:
    OMBarrier() :numThreadsRemaining(0) {}
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

#endif // STS_BARRIER_H
