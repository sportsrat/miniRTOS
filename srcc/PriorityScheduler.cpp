#include "PriorityScheduler.h"

#include "Task.h"

#include <limits>

Task* PriorityScheduler::selectNextTask(const std::vector<Task*>& readyTasks) {
    if (readyTasks.empty()) {
        return nullptr;
    }

    int highestPriority = std::numeric_limits<int>::min();
    for (Task* task : readyTasks) {
        if (task->priority() > highestPriority) {
            highestPriority = task->priority();
        }
    }

    const std::size_t startIndex = nextIndex_ % readyTasks.size();
    for (std::size_t offset = 0; offset < readyTasks.size(); ++offset) {
        const std::size_t index = (startIndex + offset) % readyTasks.size();
        if (readyTasks[index]->priority() == highestPriority) {
            nextIndex_ = (index + 1) % readyTasks.size();
            return readyTasks[index];
        }
    }
    return nullptr;
}