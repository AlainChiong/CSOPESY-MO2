#include "ScreenManager.h"

#include "MemoryManager.h"
#include "Scheduler.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
	std::string trim(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return "";

		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	bool hasValidInstructionCount(const std::string& instruction_text) {
		if (trim(instruction_text).empty()) return false;

		bool inside_quotes = false;
		bool escaped = false;
		uint32_t instruction_count = 0;
		std::string current_instruction;

		for (char character : instruction_text) {
			if (escaped) {
				current_instruction += character;
				escaped = false;
				continue;
			}

			if (character == '\\') {
				current_instruction += character;
				escaped = true;
				continue;
			}

			if (character == '"') {
				inside_quotes = !inside_quotes;
				current_instruction += character;
				continue;
			}

			if (character == ';' && !inside_quotes) {
				if (trim(current_instruction).empty()) return false;
				instruction_count++;
				current_instruction.clear();
				continue;
			}

			current_instruction += character;
		}

		if (inside_quotes || trim(current_instruction).empty()) return false;
		instruction_count++;
		return instruction_count >= 1 && instruction_count <= 50;
	}
}

ScreenManager& ScreenManager::getInstance() {
	static ScreenManager instance;
	return instance;
}

void ScreenManager::setMemoryManager(MemoryManager* manager) {
	memory_manager = manager;
}

void ScreenManager::clearScreen() const {
#ifdef _WIN32
	std::system("cls");
#else
	std::system("clear");
#endif
}

void ScreenManager::createScreen(const std::string& process_name, uint32_t memory_size) {
	if (process_name.empty()) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	if (!createProcessInScheduler(process_name, memory_size)) {
		std::cout << "Process " << process_name
			<< " already exists or could not be created." << std::endl;
		return;
	}

	runProcessScreen(process_name);
}

void ScreenManager::createCustomScreen(const std::string& process_name,
	uint32_t memory_size, const std::string& instruction_text) {
	if (process_name.empty() || !hasValidInstructionCount(instruction_text)) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	if (!createCustomProcessInScheduler(process_name, memory_size, instruction_text)) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	runProcessScreen(process_name);
}

void ScreenManager::resumeScreen(const std::string& process_name) {
	if (process_name.empty()) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	ProcessView process;
	if (!getProcessFromScheduler(process_name, process, true)) {
		std::cout << "Process " << process_name << " not found." << std::endl;
		return;
	}

	if (process.accessViolation) {
		printAccessViolation(process);
		return;
	}

	if (process.status == "FINISHED") {
		std::cout << "Process " << process_name << " not found." << std::endl;
		return;
	}

	runProcessScreen(process_name);
}

void ScreenManager::runProcessScreen(const std::string& process_name) {
	clearScreen();

	std::cout << "Attached to process screen: " << process_name << std::endl;
	std::cout << "Type 'process-smi' to view process details." << std::endl;
	std::cout << "Type 'exit' to return to the main menu." << std::endl;
	std::cout << "--------------------------------------------------" << std::endl;

	std::string command;
	while (true) {
		std::cout << "root:\\> ";
		if (!std::getline(std::cin, command)) break;

		command = trim(command);
		if (command == "exit") {
			clearScreen();
			break;
		}
		if (command.empty()) continue;

		if (command == "process-smi") {
			ProcessView process;
			if (!getProcessFromScheduler(process_name, process, true)) {
				std::cout << "Process " << process_name << " not found." << std::endl;
				continue;
			}

			if (process.accessViolation) {
				printAccessViolation(process);
				break;
			}

			displayProcessDetails(process);
			continue;
		}

		std::cout << "Invalid command inside process screen." << std::endl;
		std::cout << "Available commands: process-smi, exit" << std::endl;
	}
}

void ScreenManager::displayProcessDetails(const ProcessView& process) const {
	std::cout << std::endl;
	std::cout << "Process name: " << process.name << std::endl;
	std::cout << "ID: " << process.id << std::endl;
	std::cout << "Status: " << process.status << std::endl;
	std::cout << "Memory size: " << process.memorySize << " bytes" << std::endl;
	std::cout << "Resident memory: " << process.residentMemory << " bytes" << std::endl;
	std::cout << "Logs:" << std::endl;

	if (process.logs.empty()) {
		std::cout << "No logs available." << std::endl;
	}
	else {
		for (const std::string& log : process.logs) {
			std::cout << log << std::endl;
		}
	}

	std::cout << "Current instruction line: " << process.currentInstruction << std::endl;
	std::cout << "Lines of code: " << process.totalInstructions << std::endl;
	if (process.status == "FINISHED") std::cout << "Finished!" << std::endl;
	std::cout << std::endl;
}

void ScreenManager::printAccessViolation(const ProcessView& process) const {
	std::ostringstream address;
	address << "0x" << std::uppercase << std::hex << process.invalidAddress;

	std::cout << "Process " << process.name
		<< " shut down due to memory access violation error that occurred at "
		<< process.violationTime << ". " << address.str() << " invalid."
		<< std::endl;
}

void ScreenManager::listScreens() {
	SystemView system_view;
	if (!getSystemFromScheduler(system_view)) {
		std::cout << "Scheduler is not available." << std::endl;
		return;
	}

	printSystemSummary(std::cout, system_view);
}

void ScreenManager::displaySystemProcessSMI() {
	if (memory_manager == nullptr) {
		std::cout << "Memory manager is not available." << std::endl;
		return;
	}

	SystemView system_view;
	if (!getSystemFromScheduler(system_view)) {
		std::cout << "Scheduler is not available." << std::endl;
		return;
	}

	const uint32_t total_memory = memory_manager->getTotalMemory();
	const uint32_t used_memory = memory_manager->getUsedMemory();
	const double memory_utilization = total_memory == 0
		? 0.0
		: static_cast<double>(used_memory) * 100.0 / total_memory;

	std::cout << "--------------------------------------------------" << std::endl;
	std::cout << "| PROCESS-SMI V01.00                           |" << std::endl;
	std::cout << "--------------------------------------------------" << std::endl;
	std::cout << "CPU-Util: " << std::fixed << std::setprecision(2)
		<< system_view.cpuUtilization << "%" << std::endl;
	std::cout << "Memory Usage: " << used_memory << "B / " << total_memory << "B"
		<< std::endl;
	std::cout << "Memory Util: " << memory_utilization << "%" << std::endl;
	std::cout << "==================================================" << std::endl;
	std::cout << "Running processes and memory usage:" << std::endl;
	std::cout << "--------------------------------------------------" << std::endl;

	if (system_view.runningProcesses.empty()) {
		std::cout << "No active processes." << std::endl;
	}
	else {
		for (const ProcessView& process : system_view.runningProcesses) {
			std::cout << std::left << std::setw(20) << process.name
				<< process.residentMemory << "B" << std::endl;
		}
	}

	std::cout << "--------------------------------------------------" << std::endl;
	std::cout << std::right << std::defaultfloat;
}

void ScreenManager::displayVMStat() {
	if (memory_manager == nullptr) {
		std::cout << "Memory manager is not available." << std::endl;
		return;
	}

	Scheduler& scheduler = Scheduler::getInstance();
	std::cout << memory_manager->getTotalMemory() << " total memory" << std::endl;
	std::cout << memory_manager->getUsedMemory() << " used memory" << std::endl;
	std::cout << memory_manager->getFreeMemory() << " free memory" << std::endl;
	std::cout << scheduler.getIdleCpuTicks() << " idle cpu ticks" << std::endl;
	std::cout << scheduler.getActiveCpuTicks() << " active cpu ticks" << std::endl;
	std::cout << scheduler.getTotalCpuTicks() << " total cpu ticks" << std::endl;
	std::cout << memory_manager->getPagedIn() << " pages paged in" << std::endl;
	std::cout << memory_manager->getPagedOut() << " pages paged out" << std::endl;
}

void ScreenManager::generateReport() {
	SystemView system_view;
	if (!getSystemFromScheduler(system_view)) {
		std::cout << "Scheduler is not available." << std::endl;
		return;
	}

	std::ofstream log_file("csopesy-log.txt");
	if (!log_file.is_open()) {
		std::cout << "Error: Could not create csopesy-log.txt." << std::endl;
		return;
	}

	printSystemSummary(log_file, system_view);
	std::cout << "Report generated at csopesy-log.txt!" << std::endl;
}

void ScreenManager::printSystemSummary(std::ostream& out,
	const SystemView& system_view) const {
	out << "CPU utilization: " << std::fixed << std::setprecision(2)
		<< system_view.cpuUtilization << "%" << std::endl;
	out << "Cores used: " << system_view.coresUsed << std::endl;
	out << "Cores available: " << system_view.coresAvailable << std::endl;
	out << "--------------------------------------------------" << std::endl;
	out << "Running processes:" << std::endl;

	if (system_view.runningProcesses.empty()) {
		out << "No running processes." << std::endl;
	}
	else {
		for (const ProcessView& process : system_view.runningProcesses) {
			out << std::left << std::setw(15) << process.name
				<< " Status: " << std::setw(9) << process.status;

			if (process.core >= 0) out << " Core: " << process.core;
			else out << " Core: N/A";

			out << " " << process.currentInstruction << " / "
				<< process.totalInstructions << std::endl;
		}
	}

	out << std::endl;
	out << "Finished processes:" << std::endl;
	if (system_view.finishedProcesses.empty()) {
		out << "No finished processes." << std::endl;
	}
	else {
		for (const ProcessView& process : system_view.finishedProcesses) {
			out << std::left << std::setw(15) << process.name
				<< " Finished " << process.currentInstruction << " / "
				<< process.totalInstructions << std::endl;
		}
	}

	out << "--------------------------------------------------" << std::endl;
	out << std::right << std::defaultfloat;
}

bool ScreenManager::createProcessInScheduler(const std::string& process_name,
	uint32_t memory_size) {
	return Scheduler::getInstance().createProcess(process_name, memory_size);
}

bool ScreenManager::createCustomProcessInScheduler(const std::string& process_name,
	uint32_t memory_size, const std::string& instruction_text) {
	return Scheduler::getInstance().createProcess(process_name, memory_size,
		instruction_text);
}

bool ScreenManager::getProcessFromScheduler(const std::string& process_name,
	ProcessView& out_process, bool include_finished) {
	return Scheduler::getInstance().getProcessByName(process_name, out_process,
		include_finished);
}

bool ScreenManager::getSystemFromScheduler(SystemView& out_system) {
	Scheduler& scheduler = Scheduler::getInstance();
	out_system.cpuUtilization = scheduler.getCPUUtilization();
	out_system.coresUsed = scheduler.getUsedCores();
	out_system.coresAvailable = scheduler.getAvailableCores();
	out_system.runningProcesses = scheduler.getRunningProcessViews();
	out_system.finishedProcesses = scheduler.getFinishedProcessViews();
	return true;
}