#pragma once
#include <queue>
#include<thread>
#include<mutex>
#include<condition_variable>
// 异步写日志的日志队列
template <typename T >
class LockQueue
{
public:
    void push(const T&data)
    {
        std::lock_guard<std::mutex>lock(m_mutex);
        m_queue.push(data);
        m_condition_variable.notify_one();
    }
    T pop()
    {
        std::unique_lock<std::mutex>lock(m_mutex);
        while(m_queue.empty())
        {   
            m_condition_variable.wait(lock);
        }
        T data=std::move(m_queue.front());
        m_queue.pop();
        return data;
    }
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
};