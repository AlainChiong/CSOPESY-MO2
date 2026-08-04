#include "ConsoleManager.h"

#include "Config.h"
#include "MemoryManager.h"
#include "Scheduler.h"
#include "ScreenManager.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
	struct ParsedConfig {
		uint32_t num_cpu = 0;
		std::string scheduler;
		uint32_t quantum_cycles = 0;
		uint32_t batch_process_freq = 0;
		uint32_t min_ins = 0;
		uint32_t max_ins = 0;
		uint32_t delays_per_exec = 0;
		uint32_t max_overall_mem = 0;
		uint32_t mem_per_frame = 0;
		uint32_t min_mem_per_proc = 0;
		uint32_t max_mem_per_proc = 0;
	};

	std::string trim(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return "";

		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	bool parseUint32(const std::string& text, uint32_t& value) {
		if (text.empty()) return false;

		for (char character : text) {
			if (!std::isdigit(static_cast<unsigned char>(character))) return false;
		}

		try {
			const unsigned long long parsed = std::stoull(text);
			if (parsed > std::numeric_limits<uint32_t>::max()) return false;
			value = static_cast<uint32_t>(parsed);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool isPowerOfTwo(uint32_t value) {
		return value != 0 && (value & (value - 1)) == 0;
	}

	bool isValidMemoryValue(uint32_t value) {
		return value >= 64 && value <= 65536 && isPowerOfTwo(value);
	}

	std::string removeMatchingQuotes(const std::string& text) {
		if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
			return text.substr(1, text.size() - 2);
		}
		return text;
	}

	bool readConfigFile(const std::string& path, ParsedConfig& parsed,
		std::string& error_message) {
		std::ifstream config_file(path);
		if (!config_file.is_open()) {
			error_message = "Could not open config.txt.";
			return false;
		}

		const std::unordered_set<std::string> allowed_keys = {
			"num-cpu", "scheduler", "quantum-cycles", "batch-process-freq",
			"min-ins", "max-ins", "delays-per-exec", "max-overall-mem",
			"mem-per-frame", "min-mem-per-proc", "max-mem-per-proc"
		};

		std::unordered_map<std::string, std::string> values;
		std::string line;
		uint32_t line_number = 0;

		while (std::getline(config_file, line)) {
			line_number++;
			const size_t comment_position = line.find('#');
			if (comment_position != std::string::npos) {
				line = line.substr(0, comment_position);
			}

			line = trim(line);
			if (line.empty()) continue;

			std::istringstream line_stream(line);
			std::string key;
			line_stream >> key;

			std::string value;
			std::getline(line_stream, value);
			value = trim(value);

			if (key == "delay-per-exec") key = "delays-per-exec";

			if (allowed_keys.find(key) == allowed_keys.end()) {
				error_message = "Unknown configuration key on line " +
					std::to_string(line_number) + ": " + key;
				return false;
			}
			if (value.empty()) {
				error_message = "Missing value for " + key + ".";
				return false;
			}
			if (values.find(key) != values.end()) {
				error_message = "Duplicate configuration key: " + key;
				return false;
			}

			values[key] = value;
		}

		for (const std::string& key : allowed_keys) {
			if (values.find(key) == values.end()) {
				error_message = "Missing configuration key: " + key;
				return false;
			}
		}

		auto read_number = [&](const std::string& key, uint32_t& destination) {
			if (!parseUint32(values.at(key), destination)) {
				error_message = "Invalid numeric value for " + key + ".";
				return false;
			}
			return true;
		};

		if (!read_number("num-cpu", parsed.num_cpu) ||
			!read_number("quantum-cycles", parsed.quantum_cycles) ||
			!read_number("batch-process-freq", parsed.batch_process_freq) ||
			!read_number("min-ins", parsed.min_ins) ||
			!read_number("max-ins", parsed.max_ins) ||
			!read_number("delays-per-exec", parsed.delays_per_exec) ||
			!read_number("max-overall-mem", parsed.max_overall_mem) ||
			!read_number("mem-per-frame", parsed.mem_per_frame) ||
			!read_number("min-mem-per-proc", parsed.min_mem_per_proc) ||
			!read_number("max-mem-per-proc", parsed.max_mem_per_proc)) {
			return false;
		}

		parsed.scheduler = removeMatchingQuotes(values.at("scheduler"));

		if (parsed.num_cpu < 1 || parsed.num_cpu > 128) {
			error_message = "num-cpu must be between 1 and 128.";
			return false;
		}
		if (parsed.scheduler != "fcfs" && parsed.scheduler != "rr") {
			error_message = "scheduler must be either fcfs or rr.";
			return false;
		}
		if (parsed.scheduler == "rr" && parsed.quantum_cycles < 1) {
            error_message = "quantum-cycles must be positive when using rr.";
            return false;
        }

        if (parsed.batch_process_freq < 1 || parsed.min_ins < 1 || parsed.max_ins < 1) {
            error_message = "Batch frequency and instruction values must be positive.";
            return false;
        }

		if (parsed.min_ins > parsed.max_ins) {
			error_message = "min-ins cannot be greater than max-ins.";
			return false;
		}

		if (!isValidMemoryValue(parsed.max_overall_mem) ||
			!isValidMemoryValue(parsed.mem_per_frame) ||
			!isValidMemoryValue(parsed.min_mem_per_proc) ||
			!isValidMemoryValue(parsed.max_mem_per_proc)) {
			error_message = "Memory values must be powers of two from 64 to 65536.";
			return false;
		}
		if (parsed.mem_per_frame > parsed.max_overall_mem) {
			error_message = "mem-per-frame cannot exceed max-overall-mem.";
			return false;
		}
		if (parsed.min_mem_per_proc > parsed.max_mem_per_proc) {
			error_message = "min-mem-per-proc cannot exceed max-mem-per-proc.";
			return false;
		}
		if (parsed.min_mem_per_proc < parsed.mem_per_frame ||
			parsed.min_mem_per_proc % parsed.mem_per_frame != 0 ||
			parsed.max_mem_per_proc % parsed.mem_per_frame != 0) {
			error_message = "Process memory values must contain whole pages.";
			return false;
		}

		return true;
	}

	void applyConfig(const ParsedConfig& parsed) {
		Config::num_cpu = parsed.num_cpu;
		Config::scheduler = parsed.scheduler;
		Config::quantum_cycles = parsed.quantum_cycles;
		Config::batch_process_freq = parsed.batch_process_freq;
		Config::min_ins = parsed.min_ins;
		Config::max_ins = parsed.max_ins;
		Config::delays_per_exec = parsed.delays_per_exec;
		Config::max_overall_mem = parsed.max_overall_mem;
		Config::mem_per_frame = parsed.mem_per_frame;
		Config::min_mem_per_proc = parsed.min_mem_per_proc;
		Config::max_mem_per_proc = parsed.max_mem_per_proc;
	}

	bool hasExtraArgument(std::istringstream& stream) {
		std::string extra;
		return static_cast<bool>(stream >> extra);
	}
}

ConsoleManager& ConsoleManager::getInstance() {
	static ConsoleManager instance;
	return instance;
}

ConsoleManager::~ConsoleManager() = default;

void ConsoleManager::startConsole() {
	displayMainMenu();
	std::string command;

	while (true) {
		std::cout << "root:\\> ";
		if (!std::getline(std::cin, command)) break;

		command = trim(command);
		if (command.empty()) continue;

		if (command == "exit") {
			if (Config::is_initialized) Scheduler::getInstance().stop();
			std::cout << "Exiting CSOPESY Emulator..." << std::endl;
			break;
		}

		processCommand(command);
	}
}

void ConsoleManager::displayMainMenu() const {
	std::cout << R"(
   ____ ____   ___  ____  _____ _______   __
  / ___/ ___| / _ \|  _ \| ____/ ___\ \ / /
 | |   \___ \| | | | |_) |  _| \___ \\ V /
 | |___ ___) | |_| |  __/| |___ ___) || |
  \____|____/ \___/|_|   |_____|____/ |_|
--------------------------------------------------
Welcome to CSOPESY Emulator!

Developers:
Alain Timothy Chiong
Jandeil Dural
Widenmar Embuscado

Last updated: 08-04-2026
--------------------------------------------------
)" << std::endl;
}

void ConsoleManager::processCommand(const std::string& command) {
	std::istringstream command_stream(command);
	std::string action;
	command_stream >> action;

	if (!Config::is_initialized && action != "initialize") {
		std::cout << "Error: You must run 'initialize' first before using other commands."
			<< std::endl;
		return;
	}

	if (action == "initialize") {
		if (hasExtraArgument(command_stream)) {
			std::cout << "invalid command" << std::endl;
			return;
		}
		initialize();
	}
	else if (action == "screen") {
		processScreenCommand(command);
	}
	else if (action == "scheduler-start" && !hasExtraArgument(command_stream)) {
		Scheduler::getInstance().startBatchGeneration();
		std::cout << "Batch process generation started." << std::endl;
	}
	else if (action == "scheduler-stop" && !hasExtraArgument(command_stream)) {
		Scheduler::getInstance().stopBatchGeneration();
		std::cout << "Batch process generation stopped." << std::endl;
	}
	else if (action == "report-util" && !hasExtraArgument(command_stream)) {
		ScreenManager::getInstance().generateReport();
	}
	else if (action == "process-smi" && !hasExtraArgument(command_stream)) {
		ScreenManager::getInstance().displaySystemProcessSMI();
	}
	else if (action == "vmstat" && !hasExtraArgument(command_stream)) {
		ScreenManager::getInstance().displayVMStat();
	}
	else if (action == "clear" && !hasExtraArgument(command_stream)) {
		ScreenManager::getInstance().clearScreen();
		displayMainMenu();
	}
	else {
		std::cout << "Command not recognized: " << command << std::endl;
	}
}

void ConsoleManager::processScreenCommand(const std::string& command) {
	std::istringstream command_stream(command);
	std::string action;
	std::string flag;
	command_stream >> action >> flag;

	if (flag == "-ls") {
		if (hasExtraArgument(command_stream)) {
			std::cout << "invalid command" << std::endl;
			return;
		}
		ScreenManager::getInstance().listScreens();
		return;
	}

	if (flag == "-r") {
		std::string process_name;
		if (!(command_stream >> process_name) || hasExtraArgument(command_stream)) {
			std::cout << "invalid command" << std::endl;
			return;
		}
		ScreenManager::getInstance().resumeScreen(process_name);
		displayMainMenu();
		return;
	}

	if (flag != "-s" && flag != "-c") {
		std::cout << "invalid command" << std::endl;
		return;
	}

	std::string process_name;
	if (!(command_stream >> process_name)) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	if (flag == "-s") {
		std::string memory_text;
		uint32_t memory_size = 0;

		if (!(command_stream >> memory_text) ||
			!parseUint32(memory_text, memory_size) ||
			!isValidMemoryValue(memory_size)) {
			std::cout << "invalid memory allocation" << std::endl;
			return;
		}

		if (hasExtraArgument(command_stream)) {
			std::cout << "invalid command" << std::endl;
			return;
		}

		ScreenManager::getInstance().createScreen(process_name, memory_size);
		displayMainMenu();
		return;
	}

	std::string remaining_text;
	std::getline(command_stream, remaining_text);
	remaining_text = trim(remaining_text);

	if (remaining_text.empty()) {
		std::cout << "invalid command" << std::endl;
		return;
	}

	uint32_t memory_size = 0;
	std::string instruction_text;

	if (remaining_text.front() == '"') {
		memory_size = Config::max_mem_per_proc;
		instruction_text = remaining_text;
	}
	else {
		std::istringstream custom_stream(remaining_text);
		std::string memory_text;

		if (!(custom_stream >> memory_text) ||
			!parseUint32(memory_text, memory_size) ||
			!isValidMemoryValue(memory_size)) {
			std::cout << "invalid memory allocation" << std::endl;
			return;
		}

		std::getline(custom_stream, instruction_text);
		instruction_text = trim(instruction_text);
	}

	if (!isValidMemoryValue(memory_size)) {
		std::cout << "invalid memory allocation" << std::endl;
		return;
	}

	if (instruction_text.size() < 2 ||
		instruction_text.front() != '"' ||
		instruction_text.back() != '"') {
		std::cout << "invalid command" << std::endl;
		return;
	}

	instruction_text = instruction_text.substr(1, instruction_text.size() - 2);

	ScreenManager::getInstance().createCustomScreen(
		process_name, memory_size, instruction_text);
	displayMainMenu();
}

void ConsoleManager::initialize() {
	if (Config::is_initialized) {
		std::cout << "System is already initialized." << std::endl;
		return;
	}

	ParsedConfig parsed;
	std::string error_message;
	if (!readConfigFile("config.txt", parsed, error_message)) {
		std::cout << "Configuration error: " << error_message << std::endl;
		return;
	}

	applyConfig(parsed);
	memory_manager = std::make_unique<MemoryManager>(Config::max_overall_mem,
		Config::mem_per_frame);

	ScreenManager::getInstance().setMemoryManager(memory_manager.get());
	Scheduler::getInstance().setMemoryManager(memory_manager.get());
	Scheduler::getInstance().start();
	Config::is_initialized = true;

	std::cout << "System initialized successfully." << std::endl;
	std::cout << "Loaded Configuration - CPU Cores: " << Config::num_cpu
		<< ", Scheduler: " << Config::scheduler
		<< ", Total Memory: " << Config::max_overall_mem << " bytes"
		<< ", Frame Size: " << Config::mem_per_frame << " bytes"
		<< std::endl;
}