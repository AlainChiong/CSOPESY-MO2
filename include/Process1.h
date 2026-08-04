#ifndef PROCESS1_H
#define PROCESS1_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class ProcessState {
	READY,
	RUNNING,
	WAITING,
	FINISHED
};

class Process {
public:
	std::string name;
	int id;

	uint32_t memory_size;
	uint32_t num_pages;
	std::vector<int> page_table;
	bool memory_allocated;

	std::unordered_map<std::string, uint16_t> symbol_table;
	std::unordered_map<std::string, uint32_t> symbol_offsets;
	uint32_t next_symbol_offset;

	std::vector<std::string> instructions;
	uint32_t program_counter;
	ProcessState state;
	int assigned_core;
	uint32_t quantum_remaining;
	uint32_t delay_ticks_remaining;
	uint32_t sleep_ticks_remaining;

	bool access_violation;
	std::string violation_time;
	uint32_t invalid_address;

	std::vector<std::string> logs;
	mutable std::mutex process_mutex;

	Process(std::string process_name, int process_id, uint32_t mem_size, uint32_t pages)
		: name(std::move(process_name)),
		  id(process_id),
		  memory_size(mem_size),
		  num_pages(pages),
		  page_table(pages, -1),
		  memory_allocated(false),
		  next_symbol_offset(0),
		  program_counter(0),
		  state(ProcessState::READY),
		  assigned_core(-1),
		  quantum_remaining(0),
		  delay_ticks_remaining(0),
		  sleep_ticks_remaining(0),
		  access_violation(false),
		  invalid_address(0) {}

	Process(std::string process_name, uint32_t mem_size, uint32_t pages)
		: Process(std::move(process_name), -1, mem_size, pages) {}

	Process(const Process&) = delete;
	Process& operator=(const Process&) = delete;
};

#endif