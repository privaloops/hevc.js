#pragma once

// Persistent thread pool for WPP parallel decoding.
// Workers are created once and reused across frames.
// §9.2.2: WPP requires parallel CTU row decode with diagonal dependency.

#include <cstddef>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace hevc {

class ThreadPool {
public:
    explicit ThreadPool(int num_threads = 0);
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a job to the pool
    void submit(std::function<void()> job);

    // Wait for all submitted jobs to complete
    void wait_all();

    int num_workers() const { return static_cast<int>(workers_.size()); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;

    std::mutex mutex_;
    std::condition_variable job_available_;   // workers wait on this
    std::condition_variable jobs_done_;       // wait_all() waits on this

    std::atomic<int> active_jobs_{0};
    bool shutdown_ = false;

    void worker_loop();
};

} // namespace hevc
