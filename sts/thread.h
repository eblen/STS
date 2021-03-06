#ifndef STS_THREAD_H
#define STS_THREAD_H

#include <cassert>

#include <memory>

#include <atomic>
#include <thread>

#include "lrPool.h"
#include "range.h"

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
    Thread(int id) {
        if (id!=0) {
            thread_.reset(new std::thread([=](){id_=id; core_=-1; doWork();}));
        }
    }
    //! Disable move constructor
    Thread(Thread&&) = delete;
    //! Disable move assignment
    Thread& operator=(Thread&&) = delete;
    /*! \brief
     * Execute the whole queue of subtasks
     *
     * Gets executed for the OS thread by STS::wait and for STS created
     * threads from Thread::doWork
     */
    void processQueue();
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
    /*! \brief
     * Set thread Id
     *
     * Allow a thread to change its id. This may be needed for threads created
     * externally to this class that still need a non-zero id.
     * \param[in] id
     */
    static void setId(int id) { id_ = id; }
     /*! \brief
      * Return pinned core or -1 if thread is not pinned
      *
      * Note: This is a static method. Core returned depends on the thread executing
      * not the object. Only use Thread::getCore() not t.getCore() to avoid confusion.
      *
      * \returns core number
      */
     static int getCore() { return core_; }
     /*! \brief
      * Set pinned core
      *
      * \param[in] core number
      */
     static void setCore(int core) {
         core_ = core;
         LRPool::gpool.addCore(core);
     }
private:
    void doWork(); //function executed by worker threads

    std::unique_ptr<std::thread> thread_;
    static thread_local int id_;
    static thread_local int core_;
};

#endif // STS_THREAD_H
