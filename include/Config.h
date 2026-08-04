#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <cstdint>

struct Config {
    inline static uint32_t num_cpu = 4;
    
    inline static std::string scheduler = "rr";
    
    inline static uint32_t quantum_cycles = 5;
    
    inline static uint32_t batch_process_freq = 1;
    
    inline static uint32_t min_ins = 1000;
    
    inline static uint32_t max_ins = 2000;
    
    inline static uint32_t delays_per_exec = 0;
    

    inline static uint32_t max_overall_mem = 16384; 
    
    inline static uint32_t mem_per_frame = 64;
    
    inline static uint32_t min_mem_per_proc = 64;
    
    inline static uint32_t max_mem_per_proc = 256;

    inline static bool is_initialized = false;
};

#endif