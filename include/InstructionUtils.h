#ifndef INSTRUCTION_UTILS_H
#define INSTRUCTION_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

class MemoryManager;
class Process;

class InstructionUtils {
public:
	static std::vector<std::string> generateRandomInstructions(uint32_t min_ins, uint32_t max_ins, uint32_t memory_size);
	static std::vector<std::string> parseCustomInstructions(const std::string& raw_script);
	static bool executeInstruction(Process* process, MemoryManager* memory_manager, const std::string& instruction);
};

#endif