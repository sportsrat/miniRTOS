#include "Scheduler.h"
#include "Task.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main() {
	using namespace std::chrono_literals;

	std::vector<std::unique_ptr<Task>> tasks;
	tasks.push_back(std::make_unique<Task>(1, "Sensor", 3, [](Task& task) {
		for (int job = 0; job < 3; ++job) {
			std::cout << "Sensor job " << job + 1 << "\n";
			std::this_thread::sleep_for(5ms);
			if (job < 2) {
				task.waitForNextPeriod();
			} else {
				task.terminate();
			}
		}
	}));
	tasks.back()->configureTiming(100ms, 80ms);

	tasks.push_back(std::make_unique<Task>(2, "Network", 3, [](Task& task) {
		for (int job = 0; job < 2; ++job) {
			std::cout << "Network job " << job + 1 << "\n";
			std::this_thread::sleep_for(8ms);
			if (job < 1) {
				task.waitForNextPeriod();
			} else {
				task.terminate();
			}
		}
	}));
	tasks.back()->configureTiming(200ms, 150ms);

	tasks.push_back(std::make_unique<Task>(3, "Logger", 1, [](Task& task) {
		std::cout << "Logger job 1\n";
		std::this_thread::sleep_for(3ms);
		task.terminate();
	}));
	tasks.back()->configureTiming(500ms, 400ms);

	Scheduler scheduler;
	scheduler.setQuantum(20ms);
	for (const auto& task : tasks) {
		scheduler.addTask(*task);
	}

	std::cout << "Cooperative quantum: " << scheduler.quantum().count() << " ms\n";
	scheduler.runFor(650ms);
	scheduler.join();

	std::cout << "\n========== Timing Statistics ==========\n";
	std::cout << "Task       Releases  Avg Exec  Worst Exec  Missed Deadlines\n";
	for (const auto& task : tasks) {
		const auto runs = task->executionCount();
		const auto average = runs == 0 ? 0 :
			task->totalExecutionTimeNs() / runs / 1'000'000;
		std::cout << task->name() << "\t "
				  << task->releaseCount() << "\t   "
				  << average << " ms\t  "
				  << task->worstExecutionTimeNs() / 1'000'000 << " ms\t    "
				  << task->deadlineMisses() << "\n";
	}
	std::cout << "Scheduler decisions: " << scheduler.decisionCount() << "\n";
}
