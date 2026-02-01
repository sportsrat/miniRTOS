 #pragma once

enum class TaskState {
	READY,
	RUNNING,
	BLOCKED,
	SLEEPING,
	TERMINATED
};
