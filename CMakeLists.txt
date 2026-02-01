 cmake_minimum_required(VERSION 3.16)
 project(MiniRTOS LANGUAGES CXX)

 set(CMAKE_CXX_STANDARD 17)
 set(CMAKE_CXX_STANDARD_REQUIRED ON)
 set(CMAKE_CXX_EXTENSIONS OFF)

 add_executable(basic_scheduler
	 examples/basic_scheduler.cpp
	 srcc/Task.cpp
	 srcc/Scheduler.cpp
	 srcc/PriorityScheduler.cpp
 )

add_executable(priority_inversion
	examples/priority_inversion.cpp
	srcc/Task.cpp
	srcc/Scheduler.cpp
	srcc/PriorityScheduler.cpp
	srcc/Mutex.cpp
)

target_include_directories(priority_inversion PRIVATE include)

if(UNIX)
	target_link_libraries(priority_inversion PRIVATE pthread)
endif()

 target_include_directories(basic_scheduler PRIVATE include)

 if(UNIX)
	 target_link_libraries(basic_scheduler PRIVATE pthread)
 endif()
