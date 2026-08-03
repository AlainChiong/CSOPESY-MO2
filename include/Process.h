#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class Process {
public:
    std::string name;
    
    // Memory attributes
    uint32_t memory_size;     // Requested memory size in bytes
    uint32_t num_pages;       // Total virtual pages required
    
    // Page Table: maps virtual page index to physical frame index (-1 if not in memory)
    std::vector<int> page_table; 
    
    // Symbol table for storing up to 32 uint16_t variables 
    std::unordered_map<std::string, uint16_t> symbol_table; 
    
    // Violation tracking
    bool access_violation;
    std::string violation_time;
    uint32_t invalid_address;

    Process(std::string n, uint32_t mem_size, uint32_t pages) 
        : name(n), memory_size(mem_size), num_pages(pages), access_violation(false) {
        
        // Initialize the page table with -1 (meaning no pages are loaded yet)
        page_table.resize(num_pages, -1); 
    }
};

#endif