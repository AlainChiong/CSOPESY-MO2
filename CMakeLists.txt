cmake_minimum_required(VERSION 3.10)
project(CSOPESY_MO2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(os_emulator
	src/main.cpp
	src/ConsoleManager.cpp
	src/ScreenManager.cpp
	src/Scheduler.cpp
	src/InstructionUtils.cpp
	src/MemoryManager.cpp
)

target_include_directories(os_emulator PRIVATE include)
target_link_libraries(os_emulator PRIVATE Threads::Threads)

file(COPY "config.txt" DESTINATION ${CMAKE_BINARY_DIR})