#ifndef STS_THREAD_H
#define STS_THREAD_H

#include <cassert>

#include <deque>
#include <memory>

#include <atomic>
#include <thread>

#include "range.h"
#include "task.h"

#if (__GNUC__ == 4 && __GNUC_MINOR__ <= 7) || (defined __ICC && __ICC <= 1400)
#define thread_local __thread
#endif
#if defined _MSC_VER && _MSC_VER == 1800
#define thread_local __declspec(thread)
#endif

/*! \internal \brief
 * Every thread has one associated object of this type
 *
 * Both the OS created (master-) thread and all threads created by STS
 * have an object of this type. The later ones contain a std::thread
 * in thread_. Each object contains the queue of subtasks executed
 * by the respective thread during one step.
 */
class Thread {
public:
    /*! \brief
     * Constructor
     *
     * \param[in] id   Id given to this thread. 0 is the OS thread.
     */
    Thread(int id) :nextSubtaskId_(0) {
        if (id!=0) {
            thread_.reset(new std::thread([=](){id_=id; doWork();}));
        }
    }
    //! Disable move constructor
    Thread(Thread&&) = delete; //nextSubtaskId_ access would not be thread-safe
    //! Disable move assignment
    Thread& operator=(Thread&&) = delete;
    /*! \brief
     * Execute the whole queue of subtasks
     *
     * Gets executed for the OS thread by STS::wait and for STS created
     * threads from Thread::doWork
     */
    void processQueue();
    //! Execute the next subtask in the queue
    void processTask();
    //! Wait for thread to finish
    void join() { if (thread_) thread_->join(); }
    /*! \brief
     * Return thread Id
     *
     * Note: This is a static method. Id returned depends on the thread executing
     * not the object. Only use Thread::getID() not t.getId() to avoid confusion.
     *
     * \returns thread Id
     */
    static int getId() { return id_; }
    //! \brief Return current subtask Id
    int getCurrentSubTaskId() {
        return nextSubtaskId_ - 1;
    }
private:
    void doWork(); //function executed by worker threads

    unsigned int nextSubtaskId_;
    std::unique_ptr<std::thread> thread_;
    static thread_local int id_;
};

#endif // STS_THREAD_H
