#pragma once

#include "SchedulingPolicy.h"

#include <cstddef>

class PriorityScheduler final : public SchedulingPolicy {
public:
    Task* selectNextTask(const std::vector<Task*>& readyTasks) override;

private:
    std::size_t nextIndex_{0};
};