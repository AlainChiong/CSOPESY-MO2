#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "Process1.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct Frame {
	int frame_index;
	bool is_free;
	Process* owner;
	int page_index;
	std::vector<uint8_t> data;

	Frame(int index, uint32_t size)
		: frame_index(index),
		  is_free(true),
		  owner(nullptr),
		  page_index(-1),
		  data(size, 0) {}
};

class MemoryManager {
private:
	uint32_t total_memory;
	uint32_t mem_per_frame;
	uint32_t free_memory;
	uint32_t used_memory;
	uint32_t num_paged_in;
	uint32_t num_paged_out;

	std::vector<Frame> physical_memory;
	std::deque<int> frame_allocation_queue;
	const std::string backing_store_path = "csopesy-backing-store.txt";
	mutable std::mutex memory_mutex;

	int handlePageFault(Process* process, uint32_t page_index);
	int ensurePageLoaded(Process* process, uint32_t page_index);
	void evictPage(int frame_index);
	void loadPageFromStore(Process* process, uint32_t page_index, int frame_index);
	void savePageToStore(const Process* process, uint32_t page_index, const std::vector<uint8_t>& data);
	void removeProcessFromStore(const Process* process);
	void removeFrameFromQueue(int frame_index);
	void recordAccessViolation(Process* process, uint32_t address) const;
	static bool isPowerOfTwo(uint32_t value);

public:
	MemoryManager(uint32_t max_mem, uint32_t frame_size);
	~MemoryManager() = default;

	bool allocateMemory(Process* process);
	void deallocateMemory(Process* process);

	bool readMemory(Process* process, uint32_t address, uint16_t& out_value);
	bool writeMemory(Process* process, uint32_t address, uint16_t value);

	uint32_t getTotalMemory() const;
	uint32_t getUsedMemory() const;
	uint32_t getFreeMemory() const;
	uint32_t getPagedIn() const;
	uint32_t getPagedOut() const;
	uint32_t getResidentMemory(const Process* process) const;
};

#endif