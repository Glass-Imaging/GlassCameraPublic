/*
 * Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
 */

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <functional>
#include <future>
#include <queue>

class ThreadPool {
   public:
    explicit ThreadPool(size_t);
    template <class F, class... Args>
    decltype(auto) enqueue(F&& f, Args&&... args);
    ~ThreadPool();

   private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::packaged_task<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// add new work item to the pool
template <class F, class... Args>
decltype(auto) ThreadPool::enqueue(F&& f, Args&&... args) {
    using return_type = decltype(f(args...));

    std::packaged_task<return_type()> task(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task.get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace(std::move(task));
    }
    condition.notify_one();
    return res;
}

#endif
