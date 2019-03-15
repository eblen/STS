#ifndef _LAMBDA_RUNNER_H
#define _LAMBDA_RUNNER_H

#include <cassert>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#if (__GNUC__ == 4 && __GNUC_MINOR__ <= 7) || (defined __ICC && __ICC <= 1400)
#define thread_local __thread
#endif
#if defined _MSC_VER && _MSC_VER == 1800
#define thread_local __declspec(thread)
#endif

void setAffinity(int core);

/*! \internal \brief
 * LambdaRunner
 *
 * This class represents a "runner" (a thread of execution) that can run C++
 * lambdas. Most importantly, it can be exited and re-entered, functioning like
 * a coroutine, using the "pause" and "cont" function calls, respectively.
 */
class LambdaRunner {
public:
    LambdaRunner(int core=-1) : core_(core), finished_(true), doHalt_(false), isRunning_(true) {
        thread_.reset(new std::thread([core,this](){
            instance = this;
            if (core >= 0) {
                setAffinity(core);
            }
            while (!doHalt_) {
                pause();
                // Checking finished_ ensures that each lambda is run only once.
                // This could happen if cont() is called after finishing
                if (!finished_) {
                    lambda_();
                }
                finished_ = true;
            }
        }));
        // Calling thread waits for pause, when runner is fully initialized.
        wait();
    }

    ~LambdaRunner() {
        assert(finished_);
        doHalt_ = true;
        // TODO: Race condition: cont could be called before thread pauses.
        cont();
        thread_->join();
    }

    int getCore() {return core_;}

    // Called outside of lambda to start running a new lambda.
    // It is an error to call if current lambda is not finished.
    void run(std::function<void()> lambda) {
        assert(finished_);
        lambda_ = lambda;
        finished_ = false;
        // TODO: Race condition: cont could be called before thread pauses.
        cont();
    }
    // Called inside lambda to pause execution or halt on completion.
    // It is an error to call outside of lambda
    void pause() {
        assert(instance != nullptr);
        std::unique_lock<std::mutex> lk(mut_);
        isRunning_ = false;
        cv_.notify_all();
        while (!isRunning_) {
            cv_.wait(lk);
        }
    }

    // Called outside of lambda to resume execution
    // Does nothing if called inside lambda
    void cont() {
        std::unique_lock<std::mutex> lk(mut_);
        isRunning_ = true;
        lk.unlock();
        cv_.notify_all();
    }

    // Called outside of lambda to wait for lambda to pause
    // Does nothing if called inside lambda
    void wait() {
        std::unique_lock<std::mutex> lk(mut_);
        while (isRunning_) {
            cv_.wait(lk);
        }
    }

    bool isFinished() {return finished_;}

    // Instance for current thread or nullptr if thread was not launched
    // by a runner.
    static thread_local LambdaRunner* instance;

private:
    int core_;
    std::atomic<bool> finished_;
    std::atomic<bool> doHalt_;
    std::unique_ptr<std::thread> thread_;
    std::function<void()> lambda_;

    // Synchronization Primitives
    std::atomic<bool> isRunning_;
    std::condition_variable cv_;
    std::mutex mut_;
};

#endif // LAMBDA_RUNNER_H
