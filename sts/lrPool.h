#ifndef _LR_POOL_H
#define _LR_POOL_H

#include <cassert>
#include <map>
#include <set>
#include <vector>
#include "lambdaRunner.h"

/*! \internal \brief
 * LRPool
 *
 * This class serves as an interface between LambdaRunner (LR) and users of LR,
 * handling requests for LRs, using the "get" and "release" methods. It
 * improves performance by allowing LRs to be reused, because LR creation is
 * expensive. LRs are pinned to a certain compute core and so must be requested
 * for a certain core.
 *
 * If the application knows that cores are never shared (requests for LRs to
 * the same core never occur at the same time), it can set "shared cores" to
 * false to avoid locking and improve performance.
 */
class LRPool {
public:
    LRPool() :haveSharedCores_(true) {};

    // Add a set of cores on which lambda runners can be requested.
    // It is an error to request a core that has not been added.
    void addCores(const std::set<int> &cores) {
        std::lock_guard<std::mutex> lock(poolMut_);
        size_t index = lrs_.size();
        for (int c : cores) {
            coreToIndex_[c] = index;
            index++;
        }
        lrs_.resize(index);
    }
    void addCore(int core) {
        addCores(std::set<int>({core}));
    }

    bool getSharedCores() const {return haveSharedCores_;}
    void setSharedCores(bool ssc) {haveSharedCores_ = ssc;}

    // Checkout a new LR
    std::unique_ptr<LambdaRunner> get(int core) {
        if (haveSharedCores_) {
            return getSafe(core);
        }
        else {
            return getUnsafe(core);
        }
    }

    // Release the given LR
    void release(std::unique_ptr<LambdaRunner> &lr) {
        if (haveSharedCores_) {
            releaseSafe(lr);
        }
        else {
            releaseUnsafe(lr);
        }
    }

    // Returns map of core to number of available LRs
    std::map<int,size_t> getStats() {
        // Lock only necessary because of "addCore"
        std::lock_guard<std::mutex> lock(poolMut_);
        std::map<int,size_t> m;
        for (const auto &cti : coreToIndex_) {
            m.insert(std::pair<int, size_t>(cti.first, lrs_[cti.second].size()));
        }
        return m;
    }

    // A global pool so that users don't have to create and somehow store a pool
    // if the application only needs one, which should be the usual case.
    static LRPool gpool;

private:
    std::unique_ptr<LambdaRunner> getUnsafe(int core) {
        assert(coreToIndex_.find(core) != coreToIndex_.end());
        size_t index = coreToIndex_[core];
        if (lrs_[index].empty()) {
            return std::unique_ptr<LambdaRunner>(new LambdaRunner(core));
        }
        auto lr = std::move(lrs_[index].back());
        lrs_[index].pop_back();
        return lr;
    }
    std::unique_ptr<LambdaRunner> getSafe(int core) {
        std::lock_guard<std::mutex> lock(poolMut_);
        return getUnsafe(core);
    }

    void releaseUnsafe(std::unique_ptr<LambdaRunner> &lr) {
        assert(lr->isFinished());
        int core = lr->getCore();
        assert(coreToIndex_.find(core) != coreToIndex_.end());
        size_t index = coreToIndex_[core];
        lrs_[index].emplace_back(std::move(lr));
    }
    void releaseSafe(std::unique_ptr<LambdaRunner> &lr) {
        std::lock_guard<std::mutex> lock(poolMut_);
        releaseUnsafe(lr);
    }

    std::map<int,size_t> coreToIndex_;
    std::vector<std::vector<std::unique_ptr<LambdaRunner>>> lrs_;
    bool haveSharedCores_;
    std::mutex poolMut_;
};

#endif // _LR_POOL_H
