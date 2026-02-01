#pragma once

#include <pthread.h>
#include <vector>

class Task;

class Mutex {
public:
    explicit Mutex(bool priorityInheritance = false);
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock();
    void unlock();
    bool tryLock();

    void lock(Task& task);
    void unlock(Task& task);

    bool priorityInheritanceEnabled() const noexcept;

private:
    pthread_mutex_t mutex_{};
    pthread_mutex_t metadataMutex_{};
    Task* owner_{nullptr};
    std::vector<Task*> waiters_;
    bool priorityInheritance_{false};
};