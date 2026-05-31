#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <queue>
#include <utility>

namespace gateway::common {

// BlockingQueue 是 gatewayd 线程间传递消息的最小队列。
// 它只负责同步和退出唤醒，不理解消息内容；stop() 后 pop 会尽快返回 false。
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t max_size = 0)
        : max_size_(max_size)
    {
    }

    // 入队一条消息。队列停止后丢弃新消息，避免退出阶段重新唤醒工作线程。
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_)
                return;
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                queue_.pop();
                dropped_count_.fetch_add(1, std::memory_order_relaxed);
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    // 带超时出队。超时或队列已停止且无剩余消息时返回 false。
    bool pop(T &out, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto timeout = std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
        const bool ready = cv_.wait_for(lock, timeout, [&]() {
            return stopped_ || !queue_.empty();
        });

        if (!ready || queue_.empty())
            return false;

        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // 唤醒所有等待线程。队列中已有消息仍允许被消费，方便退出前处理响应。
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    size_t droppedCount() const
    {
        return dropped_count_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    std::atomic<size_t> dropped_count_{0};
    size_t max_size_ = 0;
    bool stopped_ = false;
};

} // namespace gateway::common
