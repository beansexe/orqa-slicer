#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <utility>
#include <vector>

namespace obn::orqa {
class BackgroundTasks {
public:
    ~BackgroundTasks() { close(); }
    bool start(std::function<void()> work)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_) return false;
        for (auto it = tasks_.begin(); it != tasks_.end();) {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) it = tasks_.erase(it);
            else ++it;
        }
        try {
            tasks_.push_back(std::async(std::launch::async, [work = std::move(work)] {
                try { work(); } catch (...) {} // Never unwind through an SDK callback thread.
            }));
            return true;
        } catch (...) { return false; }
    }
    void close()
    {
        std::vector<std::future<void>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            pending.swap(tasks_);
        }
        for (auto& task : pending) task.wait();
    }
private:
    std::mutex mutex_;
    bool closing_ = false;
    std::vector<std::future<void>> tasks_;
};
} // namespace obn::orqa
