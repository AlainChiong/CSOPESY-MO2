#include "InstructionUtils.h"
#include "Process.h"
#include "MemoryManager.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace {
    uint32_t parseHexOrDec(const std::string& str) {
        if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            return static_cast<uint32_t>(std::stoul(str, nullptr, 16));
        }
        return static_cast<uint32_t>(std::stoul(str, nullptr, 10));
    }

    uint16_t getValueOrSymbol(Process* process, const std::string& token) {
        if (process->symbol_table.find(token) != process->symbol_table.end()) {
            return process->symbol_table[token];
        }
        return static_cast<uint16_t>(parseHexOrDec(token));
    }
}

std::vector<std::string> InstructionUtils::generateRandomInstructions(uint32_t min_ins, uint32_t max_ins) {
    std::vector<std::string> instructions;
    static std::mt19937 rng(1337);
    std::uniform_int_distribution<uint32_t> count_dist(min_ins, max_ins);
    std::uniform_int_distribution<int> type_dist(0, 3);
    
    uint32_t total = count_dist(rng);
    instructions.push_back("DECLARE x 0");

    for (uint32_t i = 1; i < total; ++i) {
        int choice = type_dist(rng);
        if (choice == 0) instructions.push_back("ADD x 1");
        else if (choice == 1) instructions.push_back("SUB x 1");
        else if (choice == 2) instructions.push_back("PRINT \"Value: \" x");
        else instructions.push_back("SLEEP 1");
    }
    return instructions;
}

std::vector<std::string> InstructionUtils::parseCustomInstructions(const std::string& raw_script) {
    std::vector<std::string> instructions;
    std::stringstream ss(raw_script);
    std::string line;

    while (std::getline(ss, line, ';')) {
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            instructions.push_back(line.substr(start, end - start + 1));
        }
    }
    return instructions;
}

bool InstructionUtils::executeInstruction(Process* process, MemoryManager* memory_manager, const std::string& instruction) {
    std::stringstream ss(instruction);
    std::string op;
    ss >> op;

    if (op == "DECLARE") {
        std::string var_name;
        std::string val_str;
        ss >> var_name >> val_str;

        if (process->symbol_table.find(var_name) == process->symbol_table.end() && 
            process->symbol_table.size() >= 32) {
            process->logs.push_back("Error: Symbol table full (max 32 variables).");
            return true;
        }
        process->symbol_table[var_name] = static_cast<uint16_t>(parseHexOrDec(val_str));
    }
    else if (op == "ADD" || op == "SUB") {
        std::string var_name;
        std::string val_str;
        ss >> var_name >> val_str;

        uint16_t val = getValueOrSymbol(process, val_str);
        if (op == "ADD") process->symbol_table[var_name] += val;
        else process->symbol_table[var_name] -= val;
    }
    else if (op == "READ") {
        // READ <var_name> <hex_address>
        std::string var_name;
        std::string addr_str;
        ss >> var_name >> addr_str;

        uint32_t address = parseHexOrDec(addr_str);
        uint16_t out_val = 0;

        if (!memory_manager->readMemory(process, address, out_val)) {
            return false; // Triggers access violation shutdown[cite: 3, 8]
        }
        process->symbol_table[var_name] = out_val;
    }
    else if (op == "WRITE") {
        // WRITE <hex_address> <value_or_variable>
        std::string addr_str;
        std::string val_str;
        ss >> addr_str >> val_str;

        uint32_t address = parseHexOrDec(addr_str);
        uint16_t val = getValueOrSymbol(process, val_str);

        if (!memory_manager->writeMemory(process, address, val)) {
            return false; // Triggers access violation shutdown[cite: 3, 8]
        }
    }
    else if (op == "PRINT") {
        std::string rest;
        std::getline(ss, rest);
        size_t first = rest.find_first_not_of(" \t");
        if (first != std::string::npos) rest = rest.substr(first);

        std::string output = "";
        std::stringstream print_ss(rest);
        std::string token;
        while (print_ss >> token) {
            if (process->symbol_table.find(token) != process->symbol_table.end()) {
                output += std::to_string(process->symbol_table[token]) + " ";
            } else {
                if (token.front() == '"' && token.back() == '"') token = token.substr(1, token.size() - 2);
                output += token + " ";
            }
        }
        process->logs.push_back("Output: " + output);
    }

    return true;
}