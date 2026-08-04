#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "MemoryManager.h"
#include "Process.h"
#include "ScreenManager.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

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

	uint64_t getIdleCpuTicks() const { return idle_ticks.load(); }
	uint64_t getActiveCpuTicks() const { return active_ticks.load(); }
	uint64_t getTotalCpuTicks() const { return idle_ticks.load() + active_ticks.load();}

private:
	Scheduler() = default;
	~Scheduler();

	void cpuWorkerThread(int core_id);
	void waitingTimerThread();
	void batchGeneratorThread();
	ProcessView makeProcessView(const Process& process) const;
	static std::string stateToText(ProcessState state);

	MemoryManager* memory_manager = nullptr;
	std::vector<std::shared_ptr<Process>> process_list;
	std::queue<std::shared_ptr<Process>> ready_queue;

	std::vector<std::thread> cpu_threads;
	std::thread waiting_thread;
	std::thread generator_thread;

	mutable std::mutex scheduler_mutex;
	std::condition_variable cv_work;
	std::condition_variable cv_state;

	std::atomic<bool> running{false};
	std::atomic<bool> generating_batch{false};
	std::atomic<uint64_t> active_ticks{0};
	std::atomic<uint64_t> idle_ticks{0};
	std::atomic<int> next_process_id{1};
	std::atomic<uint32_t> next_batch_number{1};
};

#endif