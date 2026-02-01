 #include "Task.h"

#include "Scheduler.h"

 #include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
 #include <stdexcept>
#include <thread>
#include <utility>

namespace {

pthread_mutex_t logMutex = PTHREAD_MUTEX_INITIALIZER;
const auto logStart = std::chrono::steady_clock::now();

const char* stateName(TaskState state) {
	 switch (state) {
	 case TaskState::READY: return "READY";
	 case TaskState::RUNNING: return "RUNNING";
	 case TaskState::BLOCKED: return "BLOCKED";
	 case TaskState::SLEEPING: return "SLEEPING";
	 case TaskState::TERMINATED: return "TERMINATED";
	 }
	 return "UNKNOWN";
}

void logTransition(const std::string& taskName, TaskState from, TaskState to) {
	 const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		 std::chrono::steady_clock::now() - logStart).count();
	 pthread_mutex_lock(&logMutex);
	 std::cout << '[' << std::setfill('0') << std::setw(4) << elapsed << " ms] Task "
			   << taskName << ": " << stateName(from) << " -> " << stateName(to) << '\n';
	 pthread_mutex_unlock(&logMutex);
}

} // namespace

 Task::Task(std::uint32_t id, std::string name, int priority, EntryFunction entry)
	 : id_(id), name_(std::move(name)), priority_(priority), entry_(std::move(entry)) {
	 effectivePriority_ = priority_;
	 if (!entry_) {
		 throw std::invalid_argument("Task entry function must not be empty");
	 }
	 if (pthread_mutex_init(&mutex_, nullptr) != 0) {
		 throw std::runtime_error("pthread_mutex_init failed");
	 }
	 if (pthread_cond_init(&condition_, nullptr) != 0) {
		 pthread_mutex_destroy(&mutex_);
		 throw std::runtime_error("pthread_cond_init failed");
	 }
 }

 Task::~Task() {
	 if (started_ && !joined_) {
		 pthread_join(thread_, nullptr);
	 }
	 pthread_cond_destroy(&condition_);
	 pthread_mutex_destroy(&mutex_);
 }

 void Task::start() {
	 pthread_mutex_lock(&mutex_);
	 if (started_) {
		 pthread_mutex_unlock(&mutex_);
		 throw std::logic_error("Task can only be started once");
	 }
	 started_ = true;
	 pthread_mutex_unlock(&mutex_);

	 if (pthread_create(&thread_, nullptr, &Task::threadEntry, this) != 0) {
		 pthread_mutex_lock(&mutex_);
		 started_ = false;
		 pthread_mutex_unlock(&mutex_);
		 throw std::runtime_error("pthread_create failed");
	 }
 }

void Task::start(Scheduler& scheduler) {
	 pthread_mutex_lock(&mutex_);
	 if (started_) {
		 pthread_mutex_unlock(&mutex_);
		 throw std::logic_error("Task can only be started once");
	 }
	 started_ = true;
	 controlled_ = true;
	 scheduler_ = &scheduler;
	 pthread_mutex_unlock(&mutex_);

	 if (pthread_create(&thread_, nullptr, &Task::threadEntry, this) != 0) {
		 pthread_mutex_lock(&mutex_);
		 started_ = false;
		 controlled_ = false;
		 scheduler_ = nullptr;
		 pthread_mutex_unlock(&mutex_);
		 throw std::runtime_error("pthread_create failed");
	 }
}

 void Task::join() {
	 pthread_mutex_lock(&mutex_);
	 if (!started_ || joined_) {
		 pthread_mutex_unlock(&mutex_);
		 return;
	 }
	 pthread_t thread = thread_;
	 pthread_mutex_unlock(&mutex_);

	 if (pthread_join(thread, nullptr) != 0) {
		 throw std::runtime_error("pthread_join failed");
	 }

	 pthread_mutex_lock(&mutex_);
	 joined_ = true;
	 pthread_mutex_unlock(&mutex_);
 }

void Task::yield() {
	 pthread_mutex_lock(&mutex_);
	 if (!controlled_) {
		 pthread_mutex_unlock(&mutex_);
		 std::this_thread::yield();
		 return;
	 }
	 recordExecutionSegmentLocked();
	 setStateLocked(TaskState::READY);
	 turnGranted_ = false;
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);

	 scheduler->notifyTaskYielded(*this);

	 pthread_mutex_lock(&mutex_);
	 while (!turnGranted_) {
		 pthread_cond_wait(&condition_, &mutex_);
	 }
	 pthread_mutex_unlock(&mutex_);
}

void Task::block() {
	 pthread_mutex_lock(&mutex_);
	 if (!controlled_ || state_ == TaskState::TERMINATED) {
		 pthread_mutex_unlock(&mutex_);
		 return;
	 }
	 recordExecutionSegmentLocked();
	 setStateLocked(TaskState::BLOCKED);
	 turnGranted_ = false;
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);

	 scheduler->notifyTaskYielded(*this);

	 pthread_mutex_lock(&mutex_);
	 while (!turnGranted_ && state_ != TaskState::TERMINATED) {
		 pthread_cond_wait(&condition_, &mutex_);
	 }
	 pthread_mutex_unlock(&mutex_);
}

void Task::unblock() {
	 pthread_mutex_lock(&mutex_);
	 if (state_ == TaskState::BLOCKED) {
		 setStateLocked(TaskState::READY);
		 pthread_cond_signal(&condition_);
	 }
	 pthread_mutex_unlock(&mutex_);
}

void Task::sleep(std::chrono::milliseconds duration) {
	 pthread_mutex_lock(&mutex_);
	 if (!controlled_ || state_ == TaskState::TERMINATED) {
		 pthread_mutex_unlock(&mutex_);
		 std::this_thread::sleep_for(duration);
		 return;
	 }
	 recordExecutionSegmentLocked();
	 setStateLocked(TaskState::SLEEPING);
	 turnGranted_ = false;
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);

	 scheduler->notifyTaskYielded(*this);

	 const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
	 const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
		 duration - seconds);
	 timespec requested{static_cast<time_t>(seconds.count()),
		 static_cast<long>(nanoseconds.count())};
	 while (clock_nanosleep(CLOCK_MONOTONIC, 0, &requested, &requested) != 0) {
	 }

	 pthread_mutex_lock(&mutex_);
	 if (state_ != TaskState::TERMINATED) {
		 setStateLocked(TaskState::READY);
	 }
	 pthread_mutex_unlock(&mutex_);

	 waitForTurn();
}

void Task::configureTiming(std::chrono::milliseconds period,
	 std::chrono::milliseconds deadline) {
	 if (period.count() <= 0 || deadline.count() <= 0) {
		 throw std::invalid_argument("Task period and deadline must be positive");
	 }
	 pthread_mutex_lock(&mutex_);
	 if (started_) {
		 pthread_mutex_unlock(&mutex_);
		 throw std::logic_error("Task timing must be configured before start");
	 }
	 period_ = period;
	 deadline_ = deadline;
	 timingConfigured_ = true;
	 pthread_mutex_unlock(&mutex_);
}

void Task::waitForNextPeriod() {
	 pthread_mutex_lock(&mutex_);
	 if (!controlled_ || !timingConfigured_ || state_ == TaskState::TERMINATED) {
		 pthread_mutex_unlock(&mutex_);
		 return;
	 }
	 const auto now = std::chrono::steady_clock::now();
	 if (now > releaseTime_ + deadline_) {
		 ++deadlineMisses_;
	 }
	 const auto nextRelease = releaseTime_ + period_;
	 jobActive_ = false;
	 recordExecutionSegmentLocked();
	 setStateLocked(TaskState::SLEEPING);
	 turnGranted_ = false;
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);

	 scheduler->notifyTaskYielded(*this);

	 auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
		 nextRelease - std::chrono::steady_clock::now());
	 if (remaining.count() < 0) {
		 remaining = std::chrono::nanoseconds(0);
	 }
	 timespec requested{
		 static_cast<time_t>(remaining.count() / 1'000'000'000),
		 static_cast<long>(remaining.count() % 1'000'000'000)};
	 while (clock_nanosleep(CLOCK_MONOTONIC, 0, &requested, &requested) != 0) {
	 }

	 pthread_mutex_lock(&mutex_);
	 if (state_ != TaskState::TERMINATED) {
		 setStateLocked(TaskState::READY);
	 }
	 pthread_mutex_unlock(&mutex_);
	 waitForTurn();
}

void Task::terminate() {
	 pthread_mutex_lock(&mutex_);
	 if (state_ != TaskState::TERMINATED) {
		 if (timingConfigured_ && jobActive_ &&
			 std::chrono::steady_clock::now() > releaseTime_ + deadline_) {
			 ++deadlineMisses_;
		 }
		 terminationRequested_ = true;
		 recordExecutionSegmentLocked();
		 setStateLocked(TaskState::TERMINATED);
		 turnGranted_ = true;
		 pthread_cond_signal(&condition_);
	 }
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);
	 if (scheduler != nullptr) {
		 scheduler->notifyTaskTerminated(*this);
	 }
}

 std::uint32_t Task::id() const noexcept { return id_; }
 const std::string& Task::name() const noexcept { return name_; }
 int Task::priority() const noexcept {
	 pthread_mutex_lock(&mutex_);
	 const int value = effectivePriority_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
 }

 TaskState Task::state() const {
	 pthread_mutex_lock(&mutex_);
	 TaskState value = state_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
 }

std::uint64_t Task::releaseCount() const {
	 pthread_mutex_lock(&mutex_);
	 auto value = releaseCount_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
}

std::uint64_t Task::deadlineMisses() const {
	 pthread_mutex_lock(&mutex_);
	 auto value = deadlineMisses_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
}

 std::uint64_t Task::executionCount() const {
	 pthread_mutex_lock(&mutex_);
	 auto value = executionCount_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
 }

 std::uint64_t Task::totalExecutionTimeNs() const {
	 pthread_mutex_lock(&mutex_);
	 auto value = totalExecutionTimeNs_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
 }

 std::uint64_t Task::worstExecutionTimeNs() const {
	 pthread_mutex_lock(&mutex_);
	 auto value = worstExecutionTimeNs_;
	 pthread_mutex_unlock(&mutex_);
	 return value;
 }

 void* Task::threadEntry(void* argument) {
	 static_cast<Task*>(argument)->run();
	 return nullptr;
 }

 void Task::run() {
	 pthread_mutex_lock(&mutex_);
	 const bool controlled = controlled_;
	 pthread_mutex_unlock(&mutex_);
	 if (controlled) {
		 waitForTurn();
		 pthread_mutex_lock(&mutex_);
		 const bool terminateBeforeEntry = terminationRequested_;
		 pthread_mutex_unlock(&mutex_);
		 if (terminateBeforeEntry) {
			 return;
		 }
	} else {
		 pthread_mutex_lock(&mutex_);
		 setStateLocked(TaskState::RUNNING);
		 runStart_ = std::chrono::steady_clock::now();
		 executionSegmentActive_ = true;
		 pthread_mutex_unlock(&mutex_);
	 }

	 try {
		 entry_(*this);
	 } catch (...) {
		 // A pthread entry point cannot propagate an exception to its creator.
	 }
	 pthread_mutex_lock(&mutex_);
	 if (timingConfigured_ && jobActive_ &&
		 std::chrono::steady_clock::now() > releaseTime_ + deadline_) {
		 ++deadlineMisses_;
	 }
	 recordExecutionSegmentLocked();
	 if (state_ != TaskState::TERMINATED) {
		 setStateLocked(TaskState::TERMINATED);
	 }
	 Scheduler* scheduler = scheduler_;
	 pthread_mutex_unlock(&mutex_);
	 if (scheduler != nullptr) {
		 scheduler->notifyTaskTerminated(*this);
	 }
 }

void Task::waitForTurn() {
	 pthread_mutex_lock(&mutex_);
	 while (!turnGranted_) {
		 pthread_cond_wait(&condition_, &mutex_);
	 }
	 pthread_mutex_unlock(&mutex_);
}

void Task::grantExecution() {
	 pthread_mutex_lock(&mutex_);
	 turnGranted_ = true;
	 if (state_ != TaskState::TERMINATED) {
		 setStateLocked(TaskState::RUNNING);
		 runStart_ = std::chrono::steady_clock::now();
		 executionSegmentActive_ = true;
		 if (timingConfigured_ && !jobActive_) {
			 releaseTime_ = runStart_;
			 jobActive_ = true;
			 ++releaseCount_;
		 }
	 }
	 pthread_cond_signal(&condition_);
	 pthread_mutex_unlock(&mutex_);
}

void Task::recordExecutionSegmentLocked() {
	 if (!executionSegmentActive_) {
		 return;
	 }
	 const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		 std::chrono::steady_clock::now() - runStart_).count();
	 const auto elapsedUnsigned = static_cast<std::uint64_t>(elapsed);
	 ++executionCount_;
	 totalExecutionTimeNs_ += elapsedUnsigned;
	 if (elapsedUnsigned > worstExecutionTimeNs_) {
		 worstExecutionTimeNs_ = elapsedUnsigned;
	 }
	 executionSegmentActive_ = false;
}

void Task::setStateLocked(TaskState newState) {
	 if (state_ == newState) {
		 return;
	 }
	 const TaskState previousState = state_;
	 state_ = newState;
	 logTransition(name_, previousState, newState);
}

void Task::boostPriority(int priority) {
	 pthread_mutex_lock(&mutex_);
	 if (priority > effectivePriority_) {
		 effectivePriority_ = priority;
	 }
	 pthread_mutex_unlock(&mutex_);
}

void Task::restorePriority() {
	 pthread_mutex_lock(&mutex_);
	 effectivePriority_ = priority_;
	 pthread_mutex_unlock(&mutex_);
}
