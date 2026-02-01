#include "Mutex.h"
#include "Scheduler.h"
#include "Task.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

void runExperiment(bool priorityInheritance) {
    using namespace std::chrono_literals;

    Mutex resource(priorityInheritance);
    std::vector<std::unique_ptr<Task>> tasks;
    tasks.push_back(std::make_unique<Task>(1, "LOW", 1, [&resource](Task& task) {
        resource.lock(task);
        std::cout << "LOW owns the resource\n";
        task.sleep(12ms);
        std::cout << "LOW releases the resource\n";
        resource.unlock(task);
        task.terminate();
    }));
    tasks.push_back(std::make_unique<Task>(2, "HIGH", 3, [&resource](Task& task) {
        task.sleep(5ms);
        std::cout << "HIGH requests the resource\n";
        resource.lock(task);
        std::cout << "HIGH acquired the resource\n";
        resource.unlock(task);
        task.terminate();
    }));
    tasks.push_back(std::make_unique<Task>(3, "MEDIUM", 2, [](Task& task) {
        task.sleep(10ms);
        for (int iteration = 0; iteration < 3; ++iteration) {
            std::cout << "MEDIUM runs\n";
            task.yield();
        }
        task.terminate();
    }));

    Scheduler scheduler;
    scheduler.setQuantum(1ms);
    for (const auto& task : tasks) {
        scheduler.addTask(*task);
    }

    std::cout << "Mutex protocol: "
              << (priorityInheritance ? "PTHREAD_PRIO_INHERIT" : "normal")
              << "\n";
    scheduler.runFor(100ms);
    scheduler.join();
}

} // namespace

int main() {
    std::cout << "========== Normal mutex ==========" << "\n";
    runExperiment(false);
    std::cout << "\n========== Priority inheritance mutex ==========" << "\n";
    runExperiment(true);
}
