#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <mutex>

enum class ProcessState {
    READY,
    RUNNING,
    FINISHED
};

class Process {
public:
    std::string name;
    int id;
    
    
    uint32_t memory_size;   
    uint32_t num_pages;      
    std::vector<int> page_table; 
    

    std::unordered_map<std::string, uint16_t> symbol_table;
    

    std::vector<std::string> instructions;
    uint32_t program_counter;
    ProcessState state;
    int assigned_core;
    uint32_t quantum_remaining;
    

    bool access_violation;
    std::string violation_time;
    uint32_t invalid_address;


    std::vector<std::string> logs;
    mutable std::mutex process_mutex;


    Process(std::string name, int id, uint32_t mem_size, uint32_t pages) 
        : name(name), 
          id(id), 
          memory_size(mem_size), 
          num_pages(pages),
          program_counter(0), 
          state(ProcessState::READY), 
          assigned_core(-1),
          quantum_remaining(0), 
          access_violation(false), 
          invalid_address(0) {
        page_table.resize(num_pages, -1);
    }


    Process(std::string name, uint32_t mem_size, uint32_t pages) 
        : Process(name, -1, mem_size, pages) {}


    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    uint32_t getResidentMemory(uint32_t frame_size) const {
        uint32_t loaded_pages = 0;
        for (int frame : page_table) {
            if (frame != -1) loaded_pages++;
        }
        return loaded_pages * frame_size;
    }
};

#endif