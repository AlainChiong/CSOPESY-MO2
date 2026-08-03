#include "../include/MemoryManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <ctime>

MemoryManager::MemoryManager(uint32_t max_mem, uint32_t frame_size) {
    total_memory = max_mem;
    mem_per_frame = frame_size;
    free_memory = max_mem;
    used_memory = 0;
    num_paged_in = 0;
    num_paged_out = 0;

    int num_frames = total_memory / mem_per_frame;
    for (int i = 0; i < num_frames; ++i) {
        physical_memory.push_back(Frame(i, mem_per_frame));
    }

    // Initialize/Clear backing store text file on startup
    std::ofstream store(backing_store_path, std::ios::trunc);
    store.close();
}

MemoryManager::~MemoryManager() {}

bool MemoryManager::isPowerOfTwo(uint32_t x) {
    return (x > 0) && ((x & (x - 1)) == 0);
}

bool MemoryManager::allocateMemory(Process* process) {
    // Validate requested memory: Must be power of 2 and between 64 and 65536 bytes
    if (process->memory_size < 64 || process->memory_size > 65536 || !isPowerOfTwo(process->memory_size)) {
        return false; 
    }

    // Calculate required pages and assign virtual memory structure
    uint32_t num_pages = std::ceil((double)process->memory_size / mem_per_frame);
    process->num_pages = num_pages;
    process->page_table.assign(num_pages, -1); 

    // Demand Paging Principle: We do NOT load pages into physical frames yet.
    // They will be loaded on-demand when readMemory or writeMemory is called.
    return true;
}

void MemoryManager::deallocateMemory(Process* process) {
    // Free up frames when process is destroyed
    for (int frame_idx : process->page_table) {
        if (frame_idx != -1) {
            physical_memory[frame_idx].is_free = true;
            physical_memory[frame_idx].owner = nullptr;
            physical_memory[frame_idx].page_index = -1;
            free_memory += mem_per_frame;
            used_memory -= mem_per_frame;
        }
    }
}

int MemoryManager::handlePageFault(Process* process, int page_index) {
    int target_frame = -1;

    // 1. Look for an empty frame in physical memory
    for (size_t i = 0; i < physical_memory.size(); ++i) {
        if (physical_memory[i].is_free) {
            target_frame = i;
            break;
        }
    }

    // 2. If memory is full, trigger replacement algorithm (FIFO)
    if (target_frame == -1) {
        target_frame = frame_allocation_queue.front();
        frame_allocation_queue.pop();
        evictPage(target_frame);
    }

    // 3. Load the requested page into the frame
    loadPageFromStore(process, page_index, target_frame);
    
    // Update process page table and replacement queue
    process->page_table[page_index] = target_frame;
    frame_allocation_queue.push(target_frame);

    return target_frame;
}

void MemoryManager::evictPage(int frame_index) {
    Frame& frame = physical_memory[frame_index];
    if (!frame.is_free && frame.owner != nullptr) {
        
        // Write out to csopesy-backing-store.txt
        savePageToStore(frame.owner->name, frame.page_index, frame.data);
        
        // Update the victim process's page table to indicate the page is no longer in memory
        frame.owner->page_table[frame.page_index] = -1; 
        
        num_paged_out++;
        frame.is_free = true;
        frame.owner = nullptr;
        free_memory += mem_per_frame;
        used_memory -= mem_per_frame;
    }
}

void MemoryManager::loadPageFromStore(Process* process, int page_index, int frame_index) {
    Frame& frame = physical_memory[frame_index];
    frame.is_free = false;
    frame.owner = process;
    frame.page_index = page_index;
    
    // Default to 0 (uninitialized)
    std::fill(frame.data.begin(), frame.data.end(), 0); 

    std::ifstream store(backing_store_path);
    std::string line;
    std::string latest_hex_data = "";

    // Read through the file to find the most recent save for this specific process and page
    if (store.is_open()) {
        while (std::getline(store, line)) {
            std::istringstream iss(line);
            std::string p_name;
            int p_page;
            std::string hex_data;

            // Our save format is: <process_name> <page_index> <hex_data>
            if (iss >> p_name >> p_page >> hex_data) {
                if (p_name == process->name && p_page == page_index) {
                    latest_hex_data = hex_data;
                }
            }
        }
        store.close();
    }

    // If we found previously evicted data in the backing store, parse the hex back into the frame
    if (!latest_hex_data.empty()) {
        for (size_t i = 0; i < latest_hex_data.length(); i += 2) {
            std::string byteString = latest_hex_data.substr(i, 2);
            frame.data[i / 2] = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
        }
    }

    num_paged_in++;
    free_memory -= mem_per_frame;
    used_memory += mem_per_frame;
}

void MemoryManager::savePageToStore(const std::string& process_name, int page_index, const std::vector<uint8_t>& data) {
    std::ofstream store(backing_store_path, std::ios::app);
    store << process_name << " " << page_index << " ";
    
    // Convert memory data to hex layout for the text file backing store
    for (uint8_t byte : data) {
        store << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    }
    store << std::dec << "\n";
    store.close();
}

bool MemoryManager::readMemory(Process* process, uint32_t address, uint16_t& out_value) {
    // Access Validation
    if (address >= process->memory_size || address + 1 >= process->memory_size) {
        process->access_violation = true;
        process->invalid_address = address;
        
        // Record timestamp for the error message
        std::time_t t = std::time(nullptr);
        char time_str[100];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&t));
        process->violation_time = std::string(time_str);
        return false;
    }

    uint32_t page_index = address / mem_per_frame;
    uint32_t offset = address % mem_per_frame;

    int frame_idx = process->page_table[page_index];
    if (frame_idx == -1) {
        frame_idx = handlePageFault(process, page_index);
    }

    // Cross-page boundary reading
    if (offset == mem_per_frame - 1) {
        uint8_t byte1 = physical_memory[frame_idx].data[offset];
        
        uint32_t next_page_index = page_index + 1;
        int next_frame_idx = process->page_table[next_page_index];
        if (next_frame_idx == -1) next_frame_idx = handlePageFault(process, next_page_index);
        
        uint8_t byte2 = physical_memory[next_frame_idx].data[0];
        out_value = (byte2 << 8) | byte1; // Little endian assembly
    } else {
        uint8_t byte1 = physical_memory[frame_idx].data[offset];
        uint8_t byte2 = physical_memory[frame_idx].data[offset + 1];
        out_value = (byte2 << 8) | byte1;
    }

    return true;
}

bool MemoryManager::writeMemory(Process* process, uint32_t address, uint16_t value) {
    // Access Validation
    if (address >= process->memory_size || address + 1 >= process->memory_size) {
        process->access_violation = true;
        process->invalid_address = address;

        std::time_t t = std::time(nullptr);
        char time_str[100];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&t));
        process->violation_time = std::string(time_str);
        return false;
    }

    uint32_t page_index = address / mem_per_frame;
    uint32_t offset = address % mem_per_frame;

    int frame_idx = process->page_table[page_index];
    if (frame_idx == -1) {
        frame_idx = handlePageFault(process, page_index);
    }

    uint8_t byte1 = value & 0xFF;
    uint8_t byte2 = (value >> 8) & 0xFF;

    // Cross-page boundary writing
    if (offset == mem_per_frame - 1) {
        physical_memory[frame_idx].data[offset] = byte1;
        
        uint32_t next_page_index = page_index + 1;
        int next_frame_idx = process->page_table[next_page_index];
        if (next_frame_idx == -1) next_frame_idx = handlePageFault(process, next_page_index);
        
        physical_memory[next_frame_idx].data[0] = byte2;
    } else {
        physical_memory[frame_idx].data[offset] = byte1;
        physical_memory[frame_idx].data[offset + 1] = byte2;
    }

    return true;
}