#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

class MemoryManager;

struct ProcessView {
	std::string name;
	int id = -1;
	std::string status;
	int core = -1;
	uint32_t currentInstruction = 0;
	uint32_t totalInstructions = 0;
	uint32_t memorySize = 0;
	uint32_t residentMemory = 0;
	bool accessViolation = false;
	std::string violationTime;
	uint32_t invalidAddress = 0;
	std::vector<std::string> logs;
    bool customProcess = false;
};

struct SystemView {
	double cpuUtilization = 0.0;
	int coresUsed = 0;
	int coresAvailable = 0;
	std::vector<ProcessView> runningProcesses;
	std::vector<ProcessView> finishedProcesses;
};

class ScreenManager {
public:
	static ScreenManager& getInstance();

	void setMemoryManager(MemoryManager* manager);
	void createScreen(const std::string& process_name, uint32_t memory_size);
	void createCustomScreen(const std::string& process_name, uint32_t memory_size,
		const std::string& instruction_text);
	void resumeScreen(const std::string& process_name);
	void listScreens();
	void displaySystemProcessSMI();
	void displayVMStat();
	void generateReport();
	void clearScreen() const;

private:
	ScreenManager() = default;

	void runProcessScreen(const std::string& process_name);
	void displayProcessDetails(const ProcessView& process) const;
	void printAccessViolation(const ProcessView& process) const;
	void printSystemSummary(std::ostream& out, const SystemView& system_view) const;

	bool createProcessInScheduler(const std::string& process_name, uint32_t memory_size);
	bool createCustomProcessInScheduler(const std::string& process_name,
		uint32_t memory_size, const std::string& instruction_text);
	bool getProcessFromScheduler(const std::string& process_name,
		ProcessView& out_process, bool include_finished);
	bool getSystemFromScheduler(SystemView& out_system);

	MemoryManager* memory_manager = nullptr;
};

#endif