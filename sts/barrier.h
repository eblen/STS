#ifndef STS_BARRIER_H
#define STS_BARRIER_H

#include <cassert>

#include <map>
#include <string>
#include <vector>

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
template <typename T>
void wait_until(const std::atomic<T> &a, T v) {
    while (a.load() != v);
}
//! Template overload to support nullptr
template <typename T>
void wait_until(const std::atomic<T> &a, std::nullptr_t) {
    while (a.load() != nullptr);
}
/*! \brief
 * Wait for either of two variables to be set to a value
 *
 * \param[in] a1   first atomic variable
 * \param[in] v1   first value
 * \param[in] a2   second atomic variable
 * \param[in] v2   second value
 */
template <typename T, typename U>
void wait_until_or(const std::atomic<T> &a1, T v1,
                   const std::atomic<U> &a2, U v2) {
    while (a1.load() != v1 && a2.load() != v2);
}
/*! \brief
 * Wait until atomic variable a is not set to value v
 *
 * \param[in] a   atomic variable
 * \param[in] v   value
 * \returns       new value of a
 */
template <typename T>
T wait_until_not(const std::atomic<T> &a, T v) {
    T v2;
    do {
        v2 = a.load();
    } while(v2 == v);
    return v2;
}
//! Template overload to support nullptr
template <typename T>
T wait_until_not(const std::atomic<T> &a, std::nullptr_t) {
    T v2;
    do {
        v2 = a.load();
    } while(v2 == nullptr);
    return v2;
}

/*! \brief
 * Wait until atomic variable a is >= value v
 *
 * \param[in] a   atomic variable
 * \param[in] v   value
 */
template <typename T>
void wait_until_ge(const std::atomic<T> &a, T v) {
    while (a.load() < v);
}

// TODO: Asserts to check for wrong or multiple uses of barriers.

/*! \internal \brief
 * A simple many-to-one (MO) barrier.
 */
class MOBarrier {
public:
    /*! \brief
     * Constructs a new many-to-one barrier.
     *
     * \param[in] name  Barrier name
     */
    MOBarrier(std::string name = "") :id(name), isLocked(true) {
        if (!id.empty()) {
            barrierInstances_[id] = this;
        }
    }
    ~MOBarrier() {
        if (!id.empty()) {
            barrierInstances_.erase(id);
        }
    }
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
     * Check if barrier is open
     */
    bool isOpen() const {return !isLocked.load();}
    /*! \brief
     * Reset barrier
     */
    void close() {isLocked.store(true);}
    /*! \brief
     * Returns MOBarrier instance for a given id or nullptr if not found
     *
     * \param[in] id  MOBarrier instance id
     * \returns MOBarrier instance
     */
    static MOBarrier *getInstance(std::string id) {
        auto entry = barrierInstances_.find(id);
        if (entry == barrierInstances_.end()) {
            return nullptr;
        }
        else {
            return entry->second;
        }
    }
private:
    std::string id;
    std::atomic<bool> isLocked;
    static std::map<std::string, MOBarrier *> barrierInstances_;
};

/*! \internal \brief
 * A reusable many-to-one (RMO) barrier.
 * Barrier is "reusable" in that it does not need to be reset between uses,
 * which can be difficult to do correctly inside a loop.
 */
class RMOBarrier {
public:
    /*! \brief
     * Constructs a new reusable many-to-one barrier.
     * Note that constructor allocates "maxThreadId" slots, so the maximum
     * allowable id should be kept small.
     *
     * \param[in] maxThreadId  maximum thread id passed to "wait"
     * \param[in] name         Barrier name
     */
    RMOBarrier(int maxThreadId, std::string name = "") :id(name),
    locksOpened_(0), lockNum_(maxThreadId+1, 0) {
        if (!id.empty()) {
            barrierInstances_[id] = this;
        }
    }
    ~RMOBarrier() {
        if (!id.empty()) {
            barrierInstances_.erase(id);
        }
    }
    /*! \brief
     * Wait on barrier. Should be called by "M" threads
     *
     * \param[in] tid  thread id
     */
    void wait(int tid) {
        assert(tid < lockNum_.size());
        lockNum_[tid]++;
        wait_until_ge(locksOpened_, lockNum_[tid]);
    }
    /*! \brief
     * Open barrier. Should be called by "O" thread.
     */
    void open() {
        locksOpened_++;
    }
    /*! \brief
     * Returns RMOBarrier instance for a given id or nullptr if not found
     *
     * \param[in] id  RMOBarrier instance id
     * \returns RMOBarrier instance
     */
    static RMOBarrier *getInstance(std::string id) {
        auto entry = barrierInstances_.find(id);
        if (entry == barrierInstances_.end()) {
            return nullptr;
        }
        else {
            return entry->second;
        }
    }
private:
    std::string id;
    std::atomic<int> locksOpened_;
    std::vector<int> lockNum_;
    static std::map<std::string, RMOBarrier *> barrierInstances_;
};

/*! \internal \brief
 * A simple one-to-many (OM) barrier.
 */
class OMBarrier {
public:
    /*! \brief
     * Constructs a new one-to-many barrier.
     *
     * \param[in] name  Barrier name
     */
    OMBarrier(std::string name = "") :id(name), numThreadsRemaining(0) {
        if (!id.empty()) {
            barrierInstances_[id] = this;
        }
    }
    ~OMBarrier() {
        if (!id.empty()) {
            barrierInstances_.erase(id);
        }
    }
    /*! \brief
     * Register with the barrier. Should be called by "M" threads.
     */
    void markArrival() {
        numThreadsRemaining--;
    }
    /*! \brief
     * Add a new thread. This can be used for work splitting
     */
    void addThread() {
        numThreadsRemaining++;
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
    /*! \brief
     * Returns OMBarrier instance for a given id or nullptr if not found
     *
     * \param[in] id  OMBarrier instance id
     * \returns OMBarrier instance
     */
    static OMBarrier *getInstance(std::string id) {
        auto entry = barrierInstances_.find(id);
        if (entry == barrierInstances_.end()) {
            return nullptr;
        }
        else {
            return entry->second;
        }
    }

private:
    std::string id;
    std::atomic<int> numThreadsRemaining;
    static std::map<std::string, OMBarrier *> barrierInstances_;
};

/*! \internal \brief
 * A simple many-to-many (MM) barrier.
 *
 * This is a reusable barrier and so works inside loops.
 * It assumes a fixed set of exactly nt threads.
 */
class MMBarrier {
public:
    /*! \brief
     * Constructs a new many-to-many barrier.
     *
     * \param[in] nt    Number of threads
     * \param[in] name  Barrier name
     */
    MMBarrier(int nt, std::string name = "") :id(name), nthreads(nt),
                                            numWaitingThreads(0),
                                            numReleasedThreads(0) {
        assert(nt > 0);
        if (!id.empty()) {
            barrierInstances_[id] = this;
        }
    }
    ~MMBarrier() {
        if (!id.empty()) {
            barrierInstances_.erase(id);
        }
    }
    //! \brief Enter barrier
    void enter() {
        wait_until(numReleasedThreads, 0);
        numWaitingThreads.fetch_add(1);
        wait_until(numWaitingThreads, nthreads);
        if (numReleasedThreads.fetch_add(1) == nthreads-1) {
            numWaitingThreads.store(0);
            numReleasedThreads.store(0);
        }
    }
    /*! \brief
     * Get barrier id (name)
     *
     * \returns barrier id (name)
     */
    std::string getId() {
        return id;
    }
    /*! \brief
     * Returns MMBarrier instance for a given id or nullptr if not found
     *
     * \param[in] id  MMBarrier instance id
     * \returns MMBarrier instance
     */
    static MMBarrier *getInstance(std::string id) {
        auto entry = barrierInstances_.find(id);
        if (entry == barrierInstances_.end()) {
            return nullptr;
        }
        else {
            return entry->second;
        }
    }
private:
    std::string id;
    const int nthreads;
    std::atomic<int> numWaitingThreads;
    std::atomic<int> numReleasedThreads;
    static std::map<std::string, MMBarrier *> barrierInstances_;
};

#endif // STS_BARRIER_H

