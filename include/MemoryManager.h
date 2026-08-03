#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "Process.h"
#include <vector>
#include <string>
#include <queue>
#include <fstream>
#include <cstdint>

struct Frame {
    int frame_index;
    bool is_free;
    Process* owner;     // Pointer to the process currently occupying the frame
    int page_index;     // The virtual page index of the owner
    std::vector<uint8_t> data; // The physical byte data

    Frame(int index, uint32_t size) : frame_index(index), is_free(true), owner(nullptr), page_index(-1) {
        data.resize(size, 0);
    }
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
    
    // Queue for First-In-First-Out (FIFO) page replacement
    std::queue<int> frame_allocation_queue;

    const std::string backing_store_path = "csopesy-backing-store.txt";

    // Internal helper functions
    int handlePageFault(Process* process, int page_index);
    void evictPage(int frame_index);
    void loadPageFromStore(Process* process, int page_index, int frame_index);
    void savePageToStore(const std::string& process_name, int page_index, const std::vector<uint8_t>& data);
    bool isPowerOfTwo(uint32_t x);

public:
    MemoryManager(uint32_t max_mem, uint32_t frame_size);
    ~MemoryManager();

    // Setup / Teardown
    bool allocateMemory(Process* process);
    void deallocateMemory(Process* process);

    // Emulated Memory Operations
    bool readMemory(Process* process, uint32_t address, uint16_t& out_value);
    bool writeMemory(Process* process, uint32_t address, uint16_t value);

    // Getters for vmstat and process-smi
    uint32_t getTotalMemory() const { return total_memory; }
    uint32_t getUsedMemory() const { return used_memory; }
    uint32_t getFreeMemory() const { return free_memory; }
    uint32_t getPagedIn() const { return num_paged_in; }
    uint32_t getPagedOut() const { return num_paged_out; }
};

#endif