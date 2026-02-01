#pragma once

#include "SchedulingPolicy.h"

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>
#include <pthread.h>
#include <vector>

class Task;

class Scheduler {
public:
	Scheduler();
	explicit Scheduler(SchedulingPolicy& policy);
	~Scheduler();

	void addTask(Task& task);
	bool removeTask(std::uint32_t taskId);

	void start();
	void join();
	 void setQuantum(std::chrono::milliseconds quantum);
	 std::chrono::milliseconds quantum() const noexcept;
	 void runFor(std::chrono::milliseconds duration);
	Task* schedule();
	Task* tick();

	std::size_t taskCount() const noexcept;
	std::uint64_t decisionCount() const noexcept;

private:
	friend class Task;

	void notifyTaskYielded(Task& task);
	void notifyTaskTerminated(Task& task);

	std::unique_ptr<SchedulingPolicy> ownedPolicy_;
	SchedulingPolicy* policy_;
	std::vector<Task*> readyTasks_;
	bool started_{false};
	std::uint64_t decisionCount_{0};
	 std::chrono::milliseconds quantum_{20};
	Task* currentTask_{nullptr};
	pthread_mutex_t mutex_{};
	pthread_cond_t condition_{};
};
