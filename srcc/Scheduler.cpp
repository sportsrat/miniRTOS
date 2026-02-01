#include "Scheduler.h"

#include "PriorityScheduler.h"
#include "Task.h"

#include <algorithm>
#include <stdexcept>
#include <thread>

Scheduler::Scheduler()
	: ownedPolicy_(std::make_unique<PriorityScheduler>()), policy_(ownedPolicy_.get()) {
	if (pthread_mutex_init(&mutex_, nullptr) != 0 ||
		pthread_cond_init(&condition_, nullptr) != 0) {
		throw std::runtime_error("Failed to initialize scheduler synchronization");
	}
}

Scheduler::Scheduler(SchedulingPolicy& policy)
	: policy_(&policy) {
	if (pthread_mutex_init(&mutex_, nullptr) != 0 ||
		pthread_cond_init(&condition_, nullptr) != 0) {
		throw std::runtime_error("Failed to initialize scheduler synchronization");
	}
}

Scheduler::~Scheduler() {
	if (started_) {
		join();
	}
	pthread_cond_destroy(&condition_);
	pthread_mutex_destroy(&mutex_);
}

void Scheduler::addTask(Task& task) {
	if (std::find(readyTasks_.begin(), readyTasks_.end(), &task) != readyTasks_.end()) {
		throw std::logic_error("Task is already registered with the scheduler");
	}
	readyTasks_.push_back(&task);
}

bool Scheduler::removeTask(std::uint32_t taskId) {
	const auto position = std::find_if(
		readyTasks_.begin(), readyTasks_.end(),
		[taskId](const Task* task) { return task->id() == taskId; });
	if (position == readyTasks_.end()) {
		return false;
	}
	readyTasks_.erase(position);
	return true;
}

void Scheduler::start() {
	if (started_) {
		throw std::logic_error("Scheduler can only be started once");
	}
	started_ = true;
	for (Task* task : readyTasks_) {
		task->start(*this);
	}
}

void Scheduler::join() {
	for (Task* task : readyTasks_) {
		task->join();
	}
}

void Scheduler::setQuantum(std::chrono::milliseconds quantum) {
	 if (quantum.count() <= 0) {
		 throw std::invalid_argument("Scheduler quantum must be positive");
	 }
	 quantum_ = quantum;
}

std::chrono::milliseconds Scheduler::quantum() const noexcept {
	 return quantum_;
}

void Scheduler::runFor(std::chrono::milliseconds duration) {
	 if (duration.count() < 0) {
		 throw std::invalid_argument("Scheduler run duration cannot be negative");
	 }
	 if (!started_) {
		 start();
	 }
	 const auto endTime = std::chrono::steady_clock::now() + duration;
	 while (std::chrono::steady_clock::now() < endTime) {
		 tick();
		 std::this_thread::sleep_for(quantum_);
	 }
}

Task* Scheduler::schedule() {
	if (!started_) {
		throw std::logic_error("Scheduler must be started before scheduling");
	}

	std::vector<Task*> runnableTasks;
	for (Task* task : readyTasks_) {
		if (task->state() == TaskState::READY) {
			runnableTasks.push_back(task);
		}
	}
	return policy_->selectNextTask(runnableTasks);
}

Task* Scheduler::tick() {
	pthread_mutex_lock(&mutex_);
	while (currentTask_ != nullptr) {
		pthread_cond_wait(&condition_, &mutex_);
	}
	pthread_mutex_unlock(&mutex_);

	Task* selectedTask = schedule();
	if (selectedTask != nullptr) {
		pthread_mutex_lock(&mutex_);
		currentTask_ = selectedTask;
		++decisionCount_;
		pthread_mutex_unlock(&mutex_);
		selectedTask->grantExecution();
	}
	return selectedTask;
}

void Scheduler::notifyTaskYielded(Task& task) {
	pthread_mutex_lock(&mutex_);
	if (currentTask_ == &task) {
		currentTask_ = nullptr;
		pthread_cond_signal(&condition_);
	}
	pthread_mutex_unlock(&mutex_);
}

void Scheduler::notifyTaskTerminated(Task& task) {
	pthread_mutex_lock(&mutex_);
	if (currentTask_ == &task) {
		currentTask_ = nullptr;
		pthread_cond_signal(&condition_);
	}
	pthread_mutex_unlock(&mutex_);
}

std::size_t Scheduler::taskCount() const noexcept {
	return readyTasks_.size();
}

std::uint64_t Scheduler::decisionCount() const noexcept {
	return decisionCount_;
}
