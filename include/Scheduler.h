#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Process.h"
#include "MemoryManager.h"
#include "ScreenManager.h"
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class Scheduler {
public:
    static Scheduler& getInstance();
    
    void start();
    void stop();
    void setMemoryManager(MemoryManager* manager);
    
    bool createProcess(const std::string& name, uint32_t memory_size);
    bool createProcess(const std::string& name, uint32_t memory_size, const std::string& custom_ins);
    
    void startBatchGeneration();
    void stopBatchGeneration();

    bool getProcessByName(const std::string& name, ProcessView& out_view, bool include_finished);
    double getCPUUtilization() const;
    int getUsedCores() const;
    int getAvailableCores() const;
    std::vector<ProcessView> getRunningProcessViews();
    std::vector<ProcessView> getFinishedProcessViews();

    uint64_t getIdleCpuTicks() const { return idle_ticks; }
    uint64_t getActiveCpuTicks() const { return active_ticks; }
    uint64_t getTotalCpuTicks() const { return idle_ticks + active_ticks; }

private:
    Scheduler() = default;
    ~Scheduler();

    void cpuWorkerThread(int core_id);
    void batchGeneratorThread();

    MemoryManager* memory_manager = nullptr;
    std::vector<std::shared_ptr<Process>> process_list;
    std::queue<std::shared_ptr<Process>> ready_queue;

    std::vector<std::thread> cpu_threads;
    std::thread generator_thread;

    mutable std::mutex scheduler_mutex;
    std::condition_variable cv_work;

    std::atomic<bool> running{false};
    std::atomic<bool> generating_batch{false};
    std::atomic<uint64_t> active_ticks{0};
    std::atomic<uint64_t> idle_ticks{0};
    std::atomic<int> next_process_id{1};
};

#endif