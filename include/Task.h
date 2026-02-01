 #pragma once

 #include "TaskState.h"

 #include <cstdint>
#include <chrono>
 #include <functional>
 #include <pthread.h>
 #include <string>

class Scheduler;
class Mutex;

 class Task {
 public:
	 using EntryFunction = std::function<void(Task&)>;

	 Task(std::uint32_t id, std::string name, int priority, EntryFunction entry);
	 ~Task();

	 Task(const Task&) = delete;
	 Task& operator=(const Task&) = delete;

	 void start();
	 void start(Scheduler& scheduler);
	 void join();
	 void yield();
	 void block();
	 void unblock();
	 void sleep(std::chrono::milliseconds duration);
	 void configureTiming(std::chrono::milliseconds period,
		 std::chrono::milliseconds deadline);
	 void waitForNextPeriod();
	 void terminate();

	 std::uint32_t id() const noexcept;
	 const std::string& name() const noexcept;
	 int priority() const noexcept;
	 TaskState state() const;
	 std::uint64_t executionCount() const;
	 std::uint64_t totalExecutionTimeNs() const;
	 std::uint64_t worstExecutionTimeNs() const;
	 std::uint64_t releaseCount() const;
	 std::uint64_t deadlineMisses() const;

 private:
	 friend class Scheduler;
	 friend class Mutex;

	 static void* threadEntry(void* argument);
	 void run();
	 void waitForTurn();
	 void grantExecution();
	 void setStateLocked(TaskState newState);
	 void recordExecutionSegmentLocked();
	 void boostPriority(int priority);
	 void restorePriority();

	 std::uint32_t id_;
	 std::string name_;
	 int priority_;
	 int effectivePriority_;
	 EntryFunction entry_;
	 pthread_t thread_{};
	 bool started_{false};
	 bool joined_{false};
	 bool controlled_{false};
	 bool turnGranted_{false};
	 bool terminationRequested_{false};
	 Scheduler* scheduler_{nullptr};

	 mutable pthread_mutex_t mutex_;
	 pthread_cond_t condition_;
	 TaskState state_{TaskState::READY};
	 std::uint64_t executionCount_{0};
	 std::uint64_t totalExecutionTimeNs_{0};
	 std::uint64_t worstExecutionTimeNs_{0};
	 bool timingConfigured_{false};
	 std::chrono::milliseconds period_{0};
	 std::chrono::milliseconds deadline_{0};
	 std::chrono::steady_clock::time_point releaseTime_{};
	 bool jobActive_{false};
	 std::uint64_t releaseCount_{0};
	 std::uint64_t deadlineMisses_{0};
	 std::chrono::steady_clock::time_point runStart_{};
	 bool executionSegmentActive_{false};
 };
