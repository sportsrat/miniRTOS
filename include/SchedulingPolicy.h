#pragma once

#include <vector>

class Task;

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    virtual Task* selectNextTask(const std::vector<Task*>& readyTasks) = 0;
};