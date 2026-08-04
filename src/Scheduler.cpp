#include "Scheduler.h"
#include "Config.h"
#include "InstructionUtils.h"
#include <chrono>
#include <algorithm>

Scheduler& Scheduler::getInstance() {
    static Scheduler instance;
    return instance;
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::setMemoryManager(MemoryManager* manager) {
    memory_manager = manager;
}

void Scheduler::start() {
    running = true;
    for (uint32_t i = 0; i < Config::num_cpu; ++i) {
        cpu_threads.emplace_back(&Scheduler::cpuWorkerThread, this, i);
    }
}

void Scheduler::stop() {
    running = false;
    stopBatchGeneration();
    cv_work.notify_all();

    for (auto& t : cpu_threads) {
        if (t.joinable()) t.join();
    }
    cpu_threads.clear();
}

bool Scheduler::createProcess(const std::string& name, uint32_t memory_size) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    for (const auto& proc : process_list) {
        if (proc->name == name) return false;
    }

    auto proc = std::make_shared<Process>(name, next_process_id++, memory_size, 0);
    if (!memory_manager->allocateMemory(proc.get())) return false;

    proc->instructions = InstructionUtils::generateRandomInstructions(Config::min_ins, Config::max_ins);
    process_list.push_back(proc);
    ready_queue.push(proc);
    cv_work.notify_one();
    return true;
}

bool Scheduler::createProcess(const std::string& name, uint32_t memory_size, const std::string& custom_ins) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    for (const auto& proc : process_list) {
        if (proc->name == name) return false;
    }

    auto proc = std::make_shared<Process>(name, next_process_id++, memory_size, 0);
    if (!memory_manager->allocateMemory(proc.get())) return false;

    proc->instructions = InstructionUtils::parseCustomInstructions(custom_ins);
    process_list.push_back(proc);
    ready_queue.push(proc);
    cv_work.notify_one();
    return true;
}

void Scheduler::startBatchGeneration() {
    if (generating_batch) return;
    generating_batch = true;
    generator_thread = std::thread(&Scheduler::batchGeneratorThread, this);
}

void Scheduler::stopBatchGeneration() {
    generating_batch = false;
    if (generator_thread.joinable()) {
        generator_thread.join();
    }
}

void Scheduler::batchGeneratorThread() {
    static int batch_counter = 1;
    while (running && generating_batch) {
        std::this_thread::sleep_for(std::chrono::milliseconds(Config::batch_process_freq * 100));
        std::string p_name = "p" + std::to_string(batch_counter++);
        createProcess(p_name, Config::min_mem_per_proc);
    }
}

void Scheduler::cpuWorkerThread(int core_id) {
    while (running) {
        std::shared_ptr<Process> current_proc = nullptr;
        {
            std::unique_lock<std::mutex> lock(scheduler_mutex);
            cv_work.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return !ready_queue.empty() || !running;
            });

            if (!running) break;

            if (ready_queue.empty()) {
                idle_ticks++;
                continue;
            }

            current_proc = ready_queue.front();
            ready_queue.pop();
        }

        active_ticks++;
        {
            std::lock_guard<std::mutex> proc_lock(current_proc->process_mutex);
            current_proc->state = ProcessState::RUNNING;
            current_proc->assigned_core = core_id;
            current_proc->quantum_remaining = Config::quantum_cycles;
        }

        while (running) {
            bool finished = false;
            bool violation_occurred = false;

            {
                std::lock_guard<std::mutex> proc_lock(current_proc->process_mutex);
                if (current_proc->program_counter >= current_proc->instructions.size()) {
                    finished = true;
                } else {
                    std::string ins = current_proc->instructions[current_proc->program_counter];
                    if (!InstructionUtils::executeInstruction(current_proc.get(), memory_manager, ins)) {
                        violation_occurred = true;
                    } else {
                        current_proc->program_counter++;
                    }
                }
            }

            if (Config::delays_per_exec > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(Config::delays_per_exec));
            }

            if (finished || violation_occurred) {
                std::lock_guard<std::mutex> proc_lock(current_proc->process_mutex);
                current_proc->state = ProcessState::FINISHED;
                current_proc->assigned_core = -1;
                memory_manager->deallocateMemory(current_proc.get());
                break;
            }

            if (Config::scheduler == "rr") {
                std::lock_guard<std::mutex> proc_lock(current_proc->process_mutex);
                current_proc->quantum_remaining--;
                if (current_proc->quantum_remaining == 0) {
                    current_proc->state = ProcessState::READY;
                    current_proc->assigned_core = -1;
                    
                    std::lock_guard<std::mutex> lock(scheduler_mutex);
                    ready_queue.push(current_proc);
                    break;
                }
            }
        }
    }
}

bool Scheduler::getProcessByName(const std::string& name, ProcessView& out_view, bool include_finished) {
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    for (const auto& proc : process_list) {
        if (proc->name == name) {
            std::lock_guard<std::mutex> proc_lock(proc->process_mutex);
            if (!include_finished && proc->state == ProcessState::FINISHED) return false;

            out_view.name = proc->name;
            out_view.id = proc->id;
            out_view.status = (proc->state == ProcessState::RUNNING) ? "RUNNING" :
                             (proc->state == ProcessState::FINISHED) ? "FINISHED" : "READY";
            out_view.core = proc->assigned_core;
            out_view.currentInstruction = proc->program_counter;
            out_view.totalInstructions = proc->instructions.size();
            out_view.memorySize = proc->memory_size;
            out_view.residentMemory = proc->getResidentMemory(Config::mem_per_frame);
            out_view.accessViolation = proc->access_violation;
            out_view.violationTime = proc->violation_time;
            out_view.invalidAddress = proc->invalid_address;
            out_view.logs = proc->logs;
            return true;
        }
    }
    return false;
}

double Scheduler::getCPUUtilization() const {
    uint64_t total = getTotalCpuTicks();
    if (total == 0) return 0.0;
    return (static_cast<double>(active_ticks) / total) * 100.0;
}

int Scheduler::getUsedCores() const {
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    int used = 0;
    for (const auto& proc : process_list) {
        std::lock_guard<std::mutex> proc_lock(proc->process_mutex);
        if (proc->state == ProcessState::RUNNING) used++;
    }
    return used;
}

int Scheduler::getAvailableCores() const {
    return Config::num_cpu - getUsedCores();
}

std::vector<ProcessView> Scheduler::getRunningProcessViews() {
    std::vector<ProcessView> views;
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    for (const auto& proc : process_list) {
        std::lock_guard<std::mutex> proc_lock(proc->process_mutex);
        if (proc->state == ProcessState::RUNNING || proc->state == ProcessState::READY) {
            ProcessView v;
            v.name = proc->name;
            v.status = (proc->state == ProcessState::RUNNING) ? "RUNNING" : "READY";
            v.core = proc->assigned_core;
            v.currentInstruction = proc->program_counter;
            v.totalInstructions = proc->instructions.size();
            v.residentMemory = proc->getResidentMemory(Config::mem_per_frame);
            views.push_back(v);
        }
    }
    return views;
}

std::vector<ProcessView> Scheduler::getFinishedProcessViews() {
    std::vector<ProcessView> views;
    std::lock_guard<std::mutex> lock(scheduler_mutex);
    for (const auto& proc : process_list) {
        std::lock_guard<std::mutex> proc_lock(proc->process_mutex);
        if (proc->state == ProcessState::FINISHED) {
            ProcessView v;
            v.name = proc->name;
            v.status = "FINISHED";
            v.currentInstruction = proc->program_counter;
            v.totalInstructions = proc->instructions.size();
            views.push_back(v);
        }
    }
    return views;
}