#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <cstdint>

struct Config {
    // =========================================
    // MCO1 Parameters - OS Scheduler Configuration (From your old config)
    // =========================================
    
    // Number of CPUs available. 
    inline static uint32_t num_cpu = 4;
    
    // The scheduler algorithm. Note: Removed the quotes for the string value.
    inline static std::string scheduler = "rr";
    
    // The time slice given for each processor.
    inline static uint32_t quantum_cycles = 5;
    
    // The frequency of generating processes in the "scheduler-test" command.
    inline static uint32_t batch_process_freq = 1;
    
    // The minimum instructions/command per process.
    inline static uint32_t min_ins = 1000;
    
    // The maximum instructions/command per process.
    inline static uint32_t max_ins = 2000;
    
    // Delay before executing the next instruction (Note: your old config said delay-per-exec, instructions say delays-per-exec)
    inline static uint32_t delays_per_exec = 0;


    // =========================================
    // MCO2 Parameters - Multitasking OS & Memory
    // (You must coordinate with your groupmates to set these to valid powers of 2 between 64 and 65536)
    // =========================================
    
    // Maximum memory available in bytes.
    inline static uint32_t max_overall_mem = 16384; 
    
    // The size of memory in bytes per frame (and per page).
    inline static uint32_t mem_per_frame = 64;
    
    // Minimum memory required for each process.
    inline static uint32_t min_mem_per_proc = 64;
    
    // Maximum memory required for each process.
    inline static uint32_t max_mem_per_proc = 256;

    inline static bool is_initialized = false;
};

#endif