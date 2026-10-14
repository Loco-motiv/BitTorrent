#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

template <typename T>
class Queue
{
public:
    Queue() {}
    Queue(const Queue&)            = delete;
    Queue& operator=(const Queue&) = delete;
    void push(const T& value)
    {
        std::lock_guard lock(m_mutex);
        m_queue.push(value);
        m_cv.notify_one();
    }
    void push(T&& value)
    {
        std::lock_guard lock(m_mutex);
        m_queue.push(std::move(value));
        m_cv.notify_one();
    }

    T waitPop()
    {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this]()
                  { return !m_queue.empty(); });
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }
    std::optional<T> pop()
    {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty())
        {
            return std::nullopt;
        }
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    bool empty() const
    {
        std::lock_guard lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const
    {
        std::lock_guard lock(m_mutex);
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};
