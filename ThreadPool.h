#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPoolA {
public:
    ThreadPoolA(size_t);
    void enqueue(std::function<void(size_t)>&& f);
    size_t queue_size() { return task_queue_size; }
    ~ThreadPoolA();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void(size_t)> > tasks;
    size_t task_queue_size;
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPoolA::ThreadPoolA(size_t threads)
    :task_queue_size(0), stop(false)
{
    size_t idx;
    for(idx = 0; idx < threads; ++idx)
        workers.emplace_back(
            [idx, this]
            {
                for(;;)
                {
                    std::function<void(size_t)> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        while ( !this->stop && this->tasks.empty()) {
                            this->condition.wait(lock);
                        }
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                        task_queue_size--;
                    }
                    task(idx);
                }
            }
        );
}

// add new work item to the pool
void ThreadPoolA::enqueue(std::function<void(size_t)>&& f)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace(f);
        task_queue_size++;
    }
    condition.notify_one();
}

// the destructor joins all threads
inline ThreadPoolA::~ThreadPoolA()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
