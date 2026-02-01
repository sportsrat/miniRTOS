#include "Mutex.h"

#include "Task.h"

#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <vector>

Mutex::Mutex(bool priorityInheritance)
    : priorityInheritance_(priorityInheritance) {
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes) != 0) {
        throw std::runtime_error("pthread_mutexattr_init failed");
    }

    if (priorityInheritance_ &&
        pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT) != 0) {
        pthread_mutexattr_destroy(&attributes);
        throw std::runtime_error("PTHREAD_PRIO_INHERIT is not available");
    }
    if (pthread_mutex_init(&mutex_, &attributes) != 0) {
        pthread_mutexattr_destroy(&attributes);
        throw std::runtime_error("pthread_mutex_init failed");
    }
    pthread_mutexattr_destroy(&attributes);

    if (pthread_mutex_init(&metadataMutex_, nullptr) != 0) {
        pthread_mutex_destroy(&mutex_);
        throw std::runtime_error("pthread_mutex_init failed");
    }
}

Mutex::~Mutex() {
    pthread_mutex_destroy(&metadataMutex_);
    pthread_mutex_destroy(&mutex_);
}

void Mutex::lock() {
    if (pthread_mutex_lock(&mutex_) != 0) {
        throw std::runtime_error("pthread_mutex_lock failed");
    }
}

void Mutex::unlock() {
    if (pthread_mutex_unlock(&mutex_) != 0) {
        throw std::runtime_error("pthread_mutex_unlock failed");
    }
}

bool Mutex::tryLock() {
    const int result = pthread_mutex_trylock(&mutex_);
    if (result == 0) {
        return true;
    }
    if (result != EBUSY) {
        throw std::runtime_error("pthread_mutex_trylock failed");
    }
    return false;
}

void Mutex::lock(Task& task) {
    for (;;) {
        if (tryLock()) {
            pthread_mutex_lock(&metadataMutex_);
            owner_ = &task;
            pthread_mutex_unlock(&metadataMutex_);
            return;
        }

        pthread_mutex_lock(&metadataMutex_);
        if (std::find(waiters_.begin(), waiters_.end(), &task) == waiters_.end()) {
            waiters_.push_back(&task);
        }
        Task* owner = owner_;
        pthread_mutex_unlock(&metadataMutex_);

        if (priorityInheritance_ && owner != nullptr) {
            owner->boostPriority(task.priority());
        }
        task.block();
    }
}

void Mutex::unlock(Task& task) {
    pthread_mutex_lock(&metadataMutex_);
    if (owner_ != &task) {
        pthread_mutex_unlock(&metadataMutex_);
        throw std::logic_error("Task does not own mutex");
    }
    owner_ = nullptr;
    Task* nextTask = nullptr;
    if (!waiters_.empty()) {
        const auto next = std::max_element(
            waiters_.begin(), waiters_.end(),
            [](const Task* left, const Task* right) {
                return left->priority() < right->priority();
            });
        nextTask = *next;
        waiters_.erase(next);
    }
    pthread_mutex_unlock(&metadataMutex_);

    if (pthread_mutex_unlock(&mutex_) != 0) {
        throw std::runtime_error("pthread_mutex_unlock failed");
    }
    task.restorePriority();
    if (nextTask != nullptr) {
        nextTask->unblock();
    }
}

bool Mutex::priorityInheritanceEnabled() const noexcept {
    return priorityInheritance_;
}