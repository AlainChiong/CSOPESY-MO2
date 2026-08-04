#include "Scheduler.h"

#include "Config.h"
#include "InstructionUtils.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace {
	constexpr std::chrono::milliseconds CPU_CYCLE_DURATION(10);

	uint32_t rollProcessMemory() {
		std::vector<uint32_t> choices;
		uint32_t current = Config::min_mem_per_proc;
		while (current <= Config::max_mem_per_proc) {
			choices.push_back(current);
			if (current > Config::max_mem_per_proc / 2) break;
			current *= 2;
		}

		if (choices.empty()) return Config::min_mem_per_proc;
		thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_int_distribution<size_t> distribution(0, choices.size() - 1);
		return choices[distribution(generator)];
	}

	std::string makeBatchName(uint32_t number) {
		std::ostringstream output;
		output << 'p' << std::setw(2) << std::setfill('0') << number;
		return output.str();
	}
}

Scheduler& Scheduler::getInstance() {
	static Scheduler instance;
	return instance;
}

Scheduler::~Scheduler() {
	stop();
}

void Scheduler::setMemoryManager(MemoryManager* manager) {
	std::lock_guard<std::mutex> lock(scheduler_mutex);
	memory_manager = manager;
}

void Scheduler::start() {
	{
		std::lock_guard<std::mutex> lock(scheduler_mutex);
		if (running.load() || memory_manager == nullptr) return;
		running = true;
	}

	cpu_threads.reserve(Config::num_cpu);
	for (uint32_t core = 0; core < Config::num_cpu; ++core) {
		cpu_threads.emplace_back(&Scheduler::cpuWorkerThread, this,
			static_cast<int>(core));
	}
	waiting_thread = std::thread(&Scheduler::waitingTimerThread, this);
}

void Scheduler::stop() {
	if (!running.exchange(false)) {
		stopBatchGeneration();
		return;
	}

	stopBatchGeneration();
	cv_work.notify_all();
	cv_state.notify_all();

	for (std::thread& thread : cpu_threads) {
		if (thread.joinable()) thread.join();
	}
	cpu_threads.clear();
	if (waiting_thread.joinable()) waiting_thread.join();
}

bool Scheduler::createProcess(const std::string& name, uint32_t memory_size) {
	if (name.empty()) return false;

	std::lock_guard<std::mutex> lock(scheduler_mutex);
	if (memory_manager == nullptr) return false;
	for (const auto& process : process_list) {
		if (process->name == name) return false;
	}

	auto process = std::make_shared<Process>(name, next_process_id.fetch_add(1),
		memory_size, 0);
	if (!memory_manager->allocateMemory(process.get())) return false;

	process->instructions = InstructionUtils::generateRandomInstructions(
		Config::min_ins, Config::max_ins, memory_size);
	if (process->instructions.empty()) {
		memory_manager->deallocateMemory(process.get());
		return false;
	}

	process_list.push_back(process);
	ready_queue.push(process);
	cv_work.notify_one();
	return true;
}

bool Scheduler::createProcess(const std::string& name, uint32_t memory_size, const std::string& custom_ins) {
	if (name.empty()) return false;

	std::vector<std::string> instructions =
		InstructionUtils::parseCustomInstructions(custom_ins);
	if (instructions.empty()) return false;

	std::lock_guard<std::mutex> lock(scheduler_mutex);
	if (memory_manager == nullptr) return false;

	for (const auto& process : process_list) {
		if (process->name == name) return false;
	}

	auto process = std::make_shared<Process>(
		name, next_process_id.fetch_add(1), memory_size, 0);

	if (!memory_manager->allocateMemory(process.get())) return false;

	process->custom_process = true;
	process->instructions = std::move(instructions);

	process_list.push_back(process);
	ready_queue.push(process);
	cv_work.notify_one();
	return true;
}

void Scheduler::startBatchGeneration() {
	if (!running.load()) return;
	bool expected = false;
	if (!generating_batch.compare_exchange_strong(expected, true)) return;

	if (generator_thread.joinable()) generator_thread.join();
	generator_thread = std::thread(&Scheduler::batchGeneratorThread, this);
}

void Scheduler::stopBatchGeneration() {
	generating_batch = false;
	cv_state.notify_all();
	if (generator_thread.joinable()) generator_thread.join();
}

void Scheduler::batchGeneratorThread() {
	uint64_t elapsed_cycles = 0;
	while (running.load() && generating_batch.load()) {
		{
			std::unique_lock<std::mutex> lock(scheduler_mutex);
			cv_state.wait_for(lock, CPU_CYCLE_DURATION, [this] {
				return !running.load() || !generating_batch.load();
			});
		}
		if (!running.load() || !generating_batch.load()) break;

		elapsed_cycles++;
		if (elapsed_cycles < Config::batch_process_freq) continue;
		elapsed_cycles = 0;

		for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
			const std::string name = makeBatchName(
				next_batch_number.fetch_add(1));
			if (createProcess(name, rollProcessMemory())) break;
		}
	}
}

void Scheduler::waitingTimerThread() {
	while (running.load()) {
		bool process_became_ready = false;
		{
			std::unique_lock<std::mutex> lock(scheduler_mutex);
			cv_state.wait_for(lock, CPU_CYCLE_DURATION, [this] {
				return !running.load();
			});
			if (!running.load()) break;

			for (const auto& process : process_list) {
				std::lock_guard<std::mutex> process_lock(process->process_mutex);
				if (process->state != ProcessState::WAITING) continue;
				if (process->sleep_ticks_remaining > 0) {
					process->sleep_ticks_remaining--;
				}
				if (process->sleep_ticks_remaining == 0) {
					process->state = ProcessState::READY;
					ready_queue.push(process);
					process_became_ready = true;
				}
			}
		}

		if (process_became_ready) cv_work.notify_all();
	}
}

void Scheduler::cpuWorkerThread(int core_id) {
	while (running.load()) {
		std::shared_ptr<Process> current_process;
		{
			std::unique_lock<std::mutex> lock(scheduler_mutex);
			cv_work.wait_for(lock, CPU_CYCLE_DURATION, [this] {
				return !ready_queue.empty() || !running.load();
			});
			if (!running.load()) break;
			if (ready_queue.empty()) {
				idle_ticks++;
				continue;
			}

			current_process = ready_queue.front();
			ready_queue.pop();
		}

		{
			std::lock_guard<std::mutex> process_lock(
				current_process->process_mutex);
			if (current_process->state != ProcessState::READY) continue;
			current_process->state = ProcessState::RUNNING;
			current_process->assigned_core = core_id;
			current_process->quantum_remaining = Config::quantum_cycles;
		}

		bool assigned = true;
		while (running.load() && assigned) {
			std::this_thread::sleep_for(CPU_CYCLE_DURATION);
			if (!running.load()) break;

			bool should_finish = false;
			bool should_wait = false;
			bool should_requeue = false;
			{
				std::lock_guard<std::mutex> process_lock(
					current_process->process_mutex);
				if (current_process->state != ProcessState::RUNNING ||
					current_process->assigned_core != core_id) {
					assigned = false;
					continue;
				}

				active_ticks++;
				if (current_process->delay_ticks_remaining > 0) {
					current_process->delay_ticks_remaining--;
				}
				else if (current_process->program_counter >=
					current_process->instructions.size()) {
					should_finish = true;
				}
				else {
					const std::string instruction = current_process->instructions[
						current_process->program_counter];
					const bool succeeded = InstructionUtils::executeInstruction(
						current_process.get(), memory_manager, instruction);
					if (!succeeded) {
						should_finish = true;
					}
					else {
						current_process->program_counter++;
						current_process->delay_ticks_remaining =
							Config::delays_per_exec;
						if (current_process->program_counter >=
							current_process->instructions.size()) {
							should_finish = true;
						}
						else if (current_process->sleep_ticks_remaining > 0) {
							should_wait = true;
						}
					}
				}

				if (should_finish) {
					current_process->state = ProcessState::FINISHED;
					current_process->assigned_core = -1;
					current_process->sleep_ticks_remaining = 0;
					assigned = false;
				}
				else if (should_wait) {
					current_process->state = ProcessState::WAITING;
					current_process->assigned_core = -1;
					assigned = false;
				}
				else if (Config::scheduler == "rr") {
					if (current_process->quantum_remaining > 0) {
						current_process->quantum_remaining--;
					}
					if (current_process->quantum_remaining == 0) {
						current_process->state = ProcessState::READY;
						current_process->assigned_core = -1;
						should_requeue = true;
						assigned = false;
					}
				}
			}

			if (should_finish) {
				memory_manager->deallocateMemory(current_process.get());
			}
			else if (should_requeue) {
				{
					std::lock_guard<std::mutex> lock(scheduler_mutex);
					ready_queue.push(current_process);
				}
				cv_work.notify_one();
			}
		}

		if (!running.load()) {
			std::lock_guard<std::mutex> process_lock(
				current_process->process_mutex);
			if (current_process->state == ProcessState::RUNNING &&
				current_process->assigned_core == core_id) {
				current_process->state = ProcessState::READY;
				current_process->assigned_core = -1;
			}
		}
	}
}

std::string Scheduler::stateToText(ProcessState state) {
	switch (state) {
	case ProcessState::RUNNING:
		return "RUNNING";
	case ProcessState::WAITING:
		return "WAITING";
	case ProcessState::FINISHED:
		return "FINISHED";
	case ProcessState::READY:
	default:
		return "READY";
	}
}

ProcessView Scheduler::makeProcessView(const Process& process) const {
	ProcessView view;
	view.name = process.name;
	view.id = process.id;
	view.status = stateToText(process.state);
	view.core = process.assigned_core;
	view.currentInstruction = process.program_counter;
	view.totalInstructions = static_cast<uint32_t>(process.instructions.size());
	view.memorySize = process.memory_size;
	view.residentMemory = memory_manager == nullptr
		? 0
		: memory_manager->getResidentMemory(&process);
    view.customProcess = process.custom_process;
	view.accessViolation = process.access_violation;
	view.violationTime = process.violation_time;
	view.invalidAddress = process.invalid_address;
	view.logs = process.logs;
	return view;
}

bool Scheduler::getProcessByName(const std::string& name, ProcessView& out_view, bool include_finished) {
	std::lock_guard<std::mutex> lock(scheduler_mutex);
	for (const auto& process : process_list) {
		if (process->name != name) continue;
		std::lock_guard<std::mutex> process_lock(process->process_mutex);
		if (!include_finished && process->state == ProcessState::FINISHED) {
			return false;
		}
		out_view = makeProcessView(*process);
		return true;
	}
	return false;
}

double Scheduler::getCPUUtilization() const {
	if (Config::num_cpu == 0) return 0.0;

	return static_cast<double>(getUsedCores()) * 100.0 /
		static_cast<double>(Config::num_cpu);
}

int Scheduler::getUsedCores() const {
	std::lock_guard<std::mutex> lock(scheduler_mutex);
	int used_cores = 0;
	for (const auto& process : process_list) {
		std::lock_guard<std::mutex> process_lock(process->process_mutex);
		if (process->state == ProcessState::RUNNING) used_cores++;
	}
	return std::min(used_cores, static_cast<int>(Config::num_cpu));
}

int Scheduler::getAvailableCores() const {
	return std::max(0, static_cast<int>(Config::num_cpu) - getUsedCores());
}

std::vector<ProcessView> Scheduler::getRunningProcessViews() {
	std::vector<ProcessView> views;
	std::lock_guard<std::mutex> lock(scheduler_mutex);
	for (const auto& process : process_list) {
		std::lock_guard<std::mutex> process_lock(process->process_mutex);
		if (process->state != ProcessState::FINISHED) {
			views.push_back(makeProcessView(*process));
		}
	}
	return views;
}

std::vector<ProcessView> Scheduler::getFinishedProcessViews() {
	std::vector<ProcessView> views;
	std::lock_guard<std::mutex> lock(scheduler_mutex);
	for (const auto& process : process_list) {
		std::lock_guard<std::mutex> process_lock(process->process_mutex);
		if (process->state == ProcessState::FINISHED) {
			views.push_back(makeProcessView(*process));
		}
	}
	return views;
}