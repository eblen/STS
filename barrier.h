#include <atomic>
#include <cassert>
#include <string>
#include <map>

#include "sts/thread.h"

/* Simple barrier implementation
 * This is a reusable barrier and so works inside loops.
 * It assumes a fixed set of exactly nthreads.
 */
class Barrier {
public:
    Barrier(int nt, std::string name = "") :id(name), nthreads(nt),
                                            numWaitingThreads(0),
                                            numReleasedThreads(0) {
        assert(nt > 0);
        if (!id.empty()) {
            barrierInstances_[id] = this;
        }
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
    std::string getId() {
        return id;
    }
    /*! \brief
     * Returns Barrier instance for a given id or nullptr if not found
     *
     * \param[in] Barrier instance id
     * \returns Barrier instance
     */
    static Barrier *getInstance(std::string id) {
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
    std::atomic<int>  numWaitingThreads;
    std::atomic<int>  numReleasedThreads;
    static std::map<std::string, Barrier *> barrierInstances_;
};
