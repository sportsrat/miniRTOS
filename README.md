# MiniRTOS

MiniRTOS is a small RTOS-inspired task scheduler written in C++17 using POSIX threads and synchronization APIs. It is designed as a systems-learning project that makes the distinction between application-level scheduling policy and Linux kernel scheduling explicit.

## Current Scope

Implemented phases:

- Task Control Block abstraction
- Priority-based scheduler
- Equal-priority round robin
- POSIX pthread execution
- Cooperative task scheduling
- Task states and transition logging
- Periodic tasks and timing statistics
- Cooperative time quantum
- Mutexes and priority inversion experiments

Not implemented yet:

- Fixed-size inter-task message queues
- Additional pluggable policies
- Full real-time system report
- General scheduler event tracing

## Architecture

```text
Scheduler core
    Task management
    Ready-task collection
    Priority selection
    Round-robin selection
    State transitions
    Timing statistics
    Priority inheritance
          |
          v
POSIX runtime
    pthread_create()
    pthread_join()
    pthread_mutex_t
    pthread_cond_t
    clock_nanosleep()
    POSIX mutex protocols
          |
          v
Linux pthread execution
```

MiniRTOS does not replace the Linux kernel scheduler. Linux still schedules the actual pthreads. MiniRTOS controls which cooperative worker is allowed to proceed.

## Project Layout

```text
MiniTaskScheduler/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── examples/
│   ├── basic_scheduler.cpp
│   └── priority_inversion.cpp
├── include/
│   ├── Mutex.h
│   ├── PriorityScheduler.h
│   ├── Scheduler.h
│   ├── SchedulingPolicy.h
│   ├── Task.h
│   └── TaskState.h
└── srcc/
    ├── Mutex.cpp
    ├── PriorityScheduler.cpp
    ├── Scheduler.cpp
    └── Task.cpp
```

## Requirements

- Linux or WSL
- C++17 compiler
- CMake 3.16 or newer
- POSIX threads
- Make or Ninja

No external framework is required.

## Build on WSL

Use a separate build directory for WSL. The Windows `build/` directory contains a CMake cache with Windows paths and must not be reused by WSL.

```bash
cd /mnt/d/MiniTaskScheduler
cmake -S . -B build-wsl -G "Unix Makefiles"
cmake --build build-wsl --parallel
```

This builds:

```text
build-wsl/basic_scheduler
build-wsl/priority_inversion
```

## Run the Periodic Scheduler

```bash
./build-wsl/basic_scheduler
```

This example demonstrates:

- Sensor, Network, and Logger tasks
- Task priorities
- Periodic release times
- Deadlines
- Execution measurements
- Cooperative 20 ms quantum
- State transitions
- Deadline-miss statistics

Typical output includes:

```text
[0000 ms] Task Sensor: READY -> RUNNING
[0005 ms] Task Sensor: RUNNING -> SLEEPING
[0101 ms] Task Sensor: SLEEPING -> READY
[0102 ms] Task Sensor: READY -> RUNNING

========== Timing Statistics ==========
Task       Releases  Avg Exec  Worst Exec  Missed Deadlines
Sensor     3         5 ms      5 ms         0
```

Exact timestamps and task ordering can vary because Linux schedules the underlying pthreads.

## Run the Priority Inversion Experiment

```bash
./build-wsl/priority_inversion
```

The example compares:

1. A normal POSIX mutex.
2. A mutex configured with `PTHREAD_PRIO_INHERIT`.

The experiment contains:

- LOW priority task owning a resource
- HIGH priority task waiting for the resource
- MEDIUM priority task competing for execution

With priority inheritance enabled, LOW temporarily receives HIGH's effective priority and releases the mutex sooner under the MiniRTOS scheduling policy.

## Important APIs

### Task

```cpp
Task task(id, name, priority, entryFunction);

task.start();
task.join();
task.yield();
task.block();
task.unblock();
task.sleep(50ms);
task.waitForNextPeriod();
task.terminate();
```

Task entry functions receive the task object:

```cpp
[](Task& task) {
    // Work performed by the task
    task.yield();
}
```

### Scheduler

```cpp
Scheduler scheduler;
scheduler.setQuantum(20ms);
scheduler.addTask(task);
scheduler.runFor(500ms);
scheduler.join();
```

The scheduler selects only tasks in the `READY` state.

### Periodic Timing

```cpp
task.configureTiming(100ms, 80ms);
```

The task records release count, execution time, worst execution time, and missed deadlines.

## Cooperative Scheduling Limitations

The time quantum is application-level. It is not a kernel preemptive time slice.

A task must voluntarily call one of the following operations for the scheduler to regain control:

```cpp
task.yield();
task.block();
task.sleep(duration);
task.waitForNextPeriod();
task.terminate();
```

A task that performs an infinite loop without yielding can prevent other MiniRTOS tasks from running. Linux may still preempt the pthread at the kernel level, but MiniRTOS has no control point until the task cooperates.

`terminate()` is also cooperative. It marks the task terminated and the task entry function should return afterward; it does not forcibly cancel the pthread.

## Learning Experiments

Try these changes in `examples/basic_scheduler.cpp`:

- Change the scheduler quantum from `20ms` to `5ms`.
- Increase Sensor execution time beyond its deadline.
- Change task priorities and observe selection order.
- Add another periodic task.
- Remove all calls to `waitForNextPeriod()` from a task and observe the effect.

Try these changes in `examples/priority_inversion.cpp`:

- Change the MEDIUM task workload.
- Increase LOW's resource hold time.
- Compare normal mutex and priority-inheritance mutex output.
- Change the task priorities.

## Verification

The project has been compiled on Linux with:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  -Iinclude \
  examples/basic_scheduler.cpp \
  srcc/Task.cpp \
  srcc/Scheduler.cpp \
  srcc/PriorityScheduler.cpp \
  -o /tmp/basic_scheduler
```

The CMake build also compiles both executable targets successfully.
