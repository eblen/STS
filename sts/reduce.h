#ifndef STS_REDUCE_H
#define STS_REDUCE_H

#include <vector>

/*!
 * Reduction class and implementation for basic data types
 *
 * Implement reductions for other types with template specialization
 *
 * These classes are meant to be passed into parallel_for loops.
 * Threads should call collect to contribute their value, and later
 * STS automatically calls reduce at the end of the parallel_for.
 * The thread that invoked the parallel_for can then call getResult()
 * to get the final result.
 *
 * This class is thread safe when used "normally." Threads should only
 * call collect without the pos argument, so each thread has a unique
 * slot. Then reduce should only be called once after all threads
 * complete (done automatically at the end of parallel_for).
 *
 * Custom implementations should be careful to ensure thread safety.
 */
template<typename T>
class TaskReduction {
public:
    TaskReduction(std::string taskName, T init, int numThreads) :result(init) {
        values.resize(numThreads, init);
    }
    void collect(T a, size_t pos) {
        values[pos] += a;
    }
    // TODO: Allow user to provide a custom reduction function
    void reduce() {
        for (const T &i : values) {
            result += i;
        }
    }
    T getResult() {
        return result;
    }
private:
    std::vector<T> values;
    T result;
};

#endif // STS_REDUCE_H
