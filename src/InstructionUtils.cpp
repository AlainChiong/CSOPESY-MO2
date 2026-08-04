#include "InstructionUtils.h"

#include "MemoryManager.h"
#include "Process.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <utility>

namespace {
	constexpr uint32_t SYMBOL_TABLE_BYTES = 64;
	constexpr size_t MAX_SYMBOLS = 32;
	constexpr uint64_t MAX_EXPANDED_INSTRUCTIONS = 5000;

	struct PrintPart {
		bool is_literal;
		std::string text;
	};

	std::string trim(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return "";
		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	std::string uppercase(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::toupper(character));
			});
		return text;
	}

	std::string unescapeQuotes(const std::string& text) {
		std::string result;
		result.reserve(text.size());
		for (size_t index = 0; index < text.size(); ++index) {
			if (text[index] == '\\' && index + 1 < text.size() &&
				(text[index + 1] == '"' || text[index + 1] == '\\')) {
				result += text[++index];
			}
			else {
				result += text[index];
			}
		}
		return result;
	}

	bool isIdentifier(const std::string& text) {
		if (text.empty() ||
			(!std::isalpha(static_cast<unsigned char>(text.front())) &&
				text.front() != '_')) {
			return false;
		}

		return std::all_of(text.begin() + 1, text.end(),
			[](unsigned char character) {
				return std::isalnum(character) || character == '_';
			});
	}

	bool parseUnsigned64(const std::string& text, uint64_t& value) {
		const std::string clean = trim(text);
		if (clean.empty() || clean.front() == '-') return false;

		int base = 10;
		size_t digit_start = 0;
		if (clean.size() > 2 && clean[0] == '0' &&
			(clean[1] == 'x' || clean[1] == 'X')) {
			base = 16;
			digit_start = 2;
		}
		else if (clean.front() == '+') {
			digit_start = 1;
		}

		if (digit_start >= clean.size()) return false;
		for (size_t index = digit_start; index < clean.size(); ++index) {
			const unsigned char character =
				static_cast<unsigned char>(clean[index]);
			if (base == 10 && !std::isdigit(character)) return false;
			if (base == 16 && !std::isxdigit(character)) return false;
		}

		try {
			size_t parsed_length = 0;
			value = std::stoull(clean, &parsed_length, base);
			return parsed_length == clean.size();
		}
		catch (...) {
			return false;
		}
	}

	bool parseAddress(const std::string& text, uint32_t& address) {
		uint64_t parsed = 0;
		if (!parseUnsigned64(text, parsed) ||
			parsed > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		address = static_cast<uint32_t>(parsed);
		return true;
	}

	uint16_t clampUint16(uint64_t value) {
		return static_cast<uint16_t>(std::min<uint64_t>(
			value, std::numeric_limits<uint16_t>::max()));
	}

	bool isValueToken(const std::string& token) {
		uint64_t ignored = 0;
		return isIdentifier(token) || parseUnsigned64(token, ignored);
	}

	std::vector<std::string> splitTopLevel(const std::string& text, char delimiter) {
		std::vector<std::string> parts;
		std::string current;
		int parenthesis_depth = 0;
		int bracket_depth = 0;
		bool inside_quotes = false;
		bool escaped = false;

		for (char character : text) {
			if (escaped) {
				current += character;
				escaped = false;
				continue;
			}
			if (character == '\\') {
				current += character;
				escaped = true;
				continue;
			}
			if (character == '"') {
				inside_quotes = !inside_quotes;
				current += character;
				continue;
			}

			if (!inside_quotes) {
				if (character == '(') parenthesis_depth++;
				else if (character == ')') parenthesis_depth--;
				else if (character == '[') bracket_depth++;
				else if (character == ']') bracket_depth--;

				if (character == delimiter && parenthesis_depth == 0 &&
					bracket_depth == 0) {
					parts.push_back(trim(current));
					current.clear();
					continue;
				}
			}

			current += character;
		}

		parts.push_back(trim(current));
		return parts;
	}

	bool getParenthesizedBody(const std::string& instruction, size_t operation_end, std::string& body) {
		size_t open = instruction.find_first_not_of(" \t", operation_end);
		if (open == std::string::npos || instruction[open] != '(') return false;

		int depth = 0;
		bool inside_quotes = false;
		bool escaped = false;
		for (size_t index = open; index < instruction.size(); ++index) {
			const char character = instruction[index];
			if (escaped) {
				escaped = false;
				continue;
			}
			if (character == '\\') {
				escaped = true;
				continue;
			}
			if (character == '"') {
				inside_quotes = !inside_quotes;
				continue;
			}
			if (inside_quotes) continue;

			if (character == '(') depth++;
			else if (character == ')') {
				depth--;
				if (depth == 0) {
					if (!trim(instruction.substr(index + 1)).empty()) return false;
					body = instruction.substr(open + 1, index - open - 1);
					return true;
				}
				if (depth < 0) return false;
			}
		}

		return false;
	}

	std::vector<std::string> parseWhitespaceArguments(std::string text) {
		for (char& character : text) {
			if (character == ',') character = ' ';
		}

		std::vector<std::string> arguments;
		std::istringstream input(text);
		std::string argument;
		while (input >> argument) arguments.push_back(argument);
		return arguments;
	}

	bool parsePrintExpression(const std::string& expression, std::vector<PrintPart>& parts) {
		parts.clear();
		size_t index = 0;
		while (index < expression.size()) {
			while (index < expression.size() &&
				(std::isspace(static_cast<unsigned char>(expression[index])) ||
					expression[index] == '+' || expression[index] == ',')) {
				index++;
			}
			if (index >= expression.size()) break;

			if (expression[index] == '"') {
				index++;
				std::string literal;
				bool closed = false;
				while (index < expression.size()) {
					if (expression[index] == '\\' &&
						index + 1 < expression.size()) {
						literal += expression[index + 1];
						index += 2;
						continue;
					}
					if (expression[index] == '"') {
						closed = true;
						index++;
						break;
					}
					literal += expression[index++];
				}
				if (!closed) return false;
				parts.push_back({true, literal});
				continue;
			}

			const size_t start = index;
			while (index < expression.size() &&
				!std::isspace(static_cast<unsigned char>(expression[index])) &&
				expression[index] != '+' && expression[index] != ',') {
				index++;
			}
			const std::string token = expression.substr(start, index - start);
			if (!isValueToken(token)) return false;
			parts.push_back({false, token});
		}

		return !parts.empty();
	}

	bool normalizeInstruction(const std::string& raw_instruction, std::vector<std::string>& output, uint32_t depth = 0) {
		if (depth > 8 || output.size() >= MAX_EXPANDED_INSTRUCTIONS) return false;

		const std::string instruction = trim(unescapeQuotes(raw_instruction));
		if (instruction.empty()) return false;

		size_t operation_end = 0;
		while (operation_end < instruction.size() &&
			(std::isalpha(static_cast<unsigned char>(instruction[operation_end])) ||
				instruction[operation_end] == '_')) {
			operation_end++;
		}
		if (operation_end == 0) return false;
		const std::string operation = uppercase(
			instruction.substr(0, operation_end));

		std::string parenthesized_body;
		const bool has_parentheses = getParenthesizedBody(
			instruction, operation_end, parenthesized_body);

		if (operation == "FOR") {
			if (!has_parentheses) return false;
			const std::vector<std::string> arguments =
				splitTopLevel(parenthesized_body, ',');
			if (arguments.size() != 2) return false;

			std::string body = trim(arguments[0]);
			if (body.size() < 2 || body.front() != '[' || body.back() != ']') {
				return false;
			}
			body = body.substr(1, body.size() - 2);
			const std::vector<std::string> body_instructions =
				splitTopLevel(body, ',');

			uint64_t repeats = 0;
			if (!parseUnsigned64(arguments[1], repeats) || repeats == 0 ||
				repeats > 100 || body_instructions.empty()) {
				return false;
			}

			for (uint64_t repeat = 0; repeat < repeats; ++repeat) {
				for (const std::string& body_instruction : body_instructions) {
					if (!normalizeInstruction(body_instruction, output, depth + 1) ||
						output.size() > MAX_EXPANDED_INSTRUCTIONS) {
						return false;
					}
				}
			}
			return true;
		}

		if (operation == "PRINT") {
			std::string expression;
			if (has_parentheses) expression = parenthesized_body;
			else expression = trim(instruction.substr(operation_end));

			std::vector<PrintPart> parts;
			if (!parsePrintExpression(expression, parts)) return false;
			output.push_back("PRINT(" + expression + ")");
			return true;
		}

		std::vector<std::string> arguments;
		if (has_parentheses) arguments = splitTopLevel(parenthesized_body, ',');
		else arguments = parseWhitespaceArguments(
			instruction.substr(operation_end));

		if (operation == "DECLARE") {
			if (arguments.size() != 2 || !isIdentifier(arguments[0]) ||
				!isValueToken(arguments[1])) return false;
			output.push_back("DECLARE " + arguments[0] + " " + arguments[1]);
			return true;
		}

		if (operation == "ADD" || operation == "SUB" || operation == "SUBTRACT") {
			if (arguments.size() == 2) {
				arguments.insert(arguments.begin() + 1, arguments[0]);
			}
			if (arguments.size() != 3 || !isIdentifier(arguments[0]) ||
				!isValueToken(arguments[1]) || !isValueToken(arguments[2])) {
				return false;
			}
			const std::string canonical_operation =
				operation == "ADD" ? "ADD" : "SUBTRACT";
			output.push_back(canonical_operation + " " + arguments[0] + " " +
				arguments[1] + " " + arguments[2]);
			return true;
		}

		if (operation == "READ") {
			uint32_t address = 0;
			if (arguments.size() != 2 || !isIdentifier(arguments[0]) ||
				!parseAddress(arguments[1], address)) return false;
			output.push_back("READ " + arguments[0] + " " + arguments[1]);
			return true;
		}

		if (operation == "WRITE") {
			uint32_t address = 0;
			if (arguments.size() != 2 ||
				!parseAddress(arguments[0], address) ||
				!isValueToken(arguments[1])) return false;
			output.push_back("WRITE " + arguments[0] + " " + arguments[1]);
			return true;
		}

		if (operation == "SLEEP") {
			uint64_t ticks = 0;
			if (arguments.size() != 1 ||
				!parseUnsigned64(arguments[0], ticks) ||
				ticks > std::numeric_limits<uint32_t>::max()) return false;
			output.push_back("SLEEP " + std::to_string(ticks));
			return true;
		}

		return false;
	}

	bool ensureVariable(Process* process, MemoryManager* memory_manager, const std::string& name, uint32_t& offset, bool& table_full, bool& was_created) {
		table_full = false;
		was_created = false;
		const auto existing = process->symbol_offsets.find(name);
		if (existing != process->symbol_offsets.end()) {
			offset = existing->second;
			return true;
		}

		if (process->symbol_offsets.size() >= MAX_SYMBOLS ||
			process->next_symbol_offset + sizeof(uint16_t) > SYMBOL_TABLE_BYTES) {
			table_full = true;
			return true;
		}

		offset = process->next_symbol_offset;
		process->next_symbol_offset += sizeof(uint16_t);
		process->symbol_offsets[name] = offset;
		process->symbol_table[name] = 0;
		was_created = true;

		if (!memory_manager->writeMemory(process, offset, 0)) return false;
		return true;
	}

	bool readVariable(Process* process, MemoryManager* memory_manager, const std::string& name, uint16_t& value) {
		uint32_t offset = 0;
		bool table_full = false;
		bool was_created = false;
		if (!ensureVariable(process, memory_manager, name, offset, table_full,
			was_created)) return false;
		if (table_full) {
			value = 0;
			return true;
		}

		if (!memory_manager->readMemory(process, offset, value)) return false;
		process->symbol_table[name] = value;
		return true;
	}

	bool writeVariable(Process* process, MemoryManager* memory_manager, const std::string& name, uint16_t value, bool& ignored) {
		uint32_t offset = 0;
		bool was_created = false;
		if (!ensureVariable(process, memory_manager, name, offset, ignored,
			was_created)) return false;
		if (ignored) return true;

		if (!memory_manager->writeMemory(process, offset, value)) return false;
		process->symbol_table[name] = value;
		return true;
	}

	bool readValue(Process* process, MemoryManager* memory_manager, const std::string& token, uint16_t& value) {
		uint64_t numeric_value = 0;
		if (parseUnsigned64(token, numeric_value)) {
			value = clampUint16(numeric_value);
			return true;
		}
		if (!isIdentifier(token)) return false;
		return readVariable(process, memory_manager, token, value);
	}

	std::string formatHexAddress(uint32_t address) {
		std::ostringstream output;
		output << "0x" << std::uppercase << std::hex << address;
		return output.str();
	}

	uint32_t randomValidAddress(std::mt19937& generator, uint32_t memory_size) {
		const uint32_t first_address = memory_size > SYMBOL_TABLE_BYTES + 1
			? SYMBOL_TABLE_BYTES
			: 0;
		const uint32_t last_address = memory_size - sizeof(uint16_t);
		const uint32_t first_slot = (first_address + 1) / 2;
		const uint32_t last_slot = last_address / 2;
		std::uniform_int_distribution<uint32_t> address_distribution(
			first_slot, last_slot);
		return address_distribution(generator) * 2;
	}
}

std::vector<std::string> InstructionUtils::generateRandomInstructions(uint32_t min_ins, uint32_t max_ins, uint32_t memory_size) {
	if (memory_size < 64) return {};
	if (min_ins == 0) min_ins = 1;
	if (max_ins < min_ins) std::swap(min_ins, max_ins);

	thread_local std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<uint32_t> count_distribution(min_ins, max_ins);
	std::uniform_int_distribution<uint32_t> value_distribution(0, 65535);
	std::uniform_int_distribution<uint32_t> small_value_distribution(0, 100);
	std::uniform_int_distribution<uint32_t> sleep_distribution(1, 5);
	std::uniform_int_distribution<int> type_distribution(0, 6);

	const uint32_t total = count_distribution(generator);
	const uint32_t shared_address = randomValidAddress(generator, memory_size);
	std::vector<std::string> instructions;
	instructions.reserve(total);

	if (total >= 1) {
		instructions.push_back("DECLARE x " +
			std::to_string(value_distribution(generator)));
	}
	if (total >= 2) {
		instructions.push_back("WRITE " + formatHexAddress(shared_address) +
			" x");
	}
	if (total >= 3) {
		instructions.push_back("READ y " + formatHexAddress(shared_address));
	}

	while (instructions.size() < total) {
		const int choice = type_distribution(generator);
		const uint32_t value = small_value_distribution(generator);
		const uint32_t address = randomValidAddress(generator, memory_size);
		switch (choice) {
		case 0:
			instructions.push_back("ADD x x " + std::to_string(value));
			break;
		case 1:
			instructions.push_back("SUBTRACT x x " + std::to_string(value));
			break;
		case 2:
			instructions.push_back("PRINT(\"Value: \" + x)");
			break;
		case 3:
			instructions.push_back("SLEEP " +
				std::to_string(sleep_distribution(generator)));
			break;
		case 4:
			instructions.push_back("WRITE " + formatHexAddress(address) + " x");
			break;
		case 5:
			instructions.push_back("READ y " + formatHexAddress(address));
			break;
		default:
			instructions.push_back("DECLARE z " + std::to_string(value));
			break;
		}
	}

	return instructions;
}

std::vector<std::string> InstructionUtils::parseCustomInstructions(const std::string& raw_script) {
	std::vector<std::string> raw_instructions;
	std::string current;
	bool inside_quotes = false;
	bool escaped = false;

	for (char character : raw_script) {
		if (escaped) {
			current += character;
			escaped = false;
			continue;
		}
		if (character == '\\') {
			current += character;
			escaped = true;
			continue;
		}
		if (character == '"') {
			inside_quotes = !inside_quotes;
			current += character;
			continue;
		}
		if (character == ';' && !inside_quotes) {
			if (trim(current).empty()) return {};
			raw_instructions.push_back(current);
			current.clear();
			continue;
		}
		current += character;
	}

	if (inside_quotes || escaped || trim(current).empty()) return {};
	raw_instructions.push_back(current);
	if (raw_instructions.empty() || raw_instructions.size() > 50) return {};

	std::vector<std::string> normalized;
	for (const std::string& instruction : raw_instructions) {
		if (!normalizeInstruction(instruction, normalized)) return {};
	}
	return normalized;
}

bool InstructionUtils::executeInstruction(Process* process, MemoryManager* memory_manager, const std::string& instruction) {
	if (process == nullptr || memory_manager == nullptr) return false;

	std::vector<std::string> normalized;
	if (!normalizeInstruction(instruction, normalized) || normalized.size() != 1) {
		return false;
	}
	const std::string& canonical = normalized.front();

	if (canonical.rfind("PRINT(", 0) == 0) {
		std::string expression;
		if (!getParenthesizedBody(canonical, 5, expression)) return false;
		std::vector<PrintPart> parts;
		if (!parsePrintExpression(expression, parts)) return false;

		std::string output;
		for (const PrintPart& part : parts) {
			if (part.is_literal) {
				output += part.text;
				continue;
			}

			uint16_t value = 0;
			if (!readValue(process, memory_manager, part.text, value)) return false;
			output += std::to_string(value);
		}
		process->logs.push_back("Output: " + output);
		return true;
	}

	std::istringstream input(canonical);
	std::string operation;
	input >> operation;

	if (operation == "DECLARE") {
		std::string destination;
		std::string source;
		input >> destination >> source;
		if (process->symbol_offsets.find(destination) ==
				process->symbol_offsets.end() &&
			process->symbol_offsets.size() >= MAX_SYMBOLS) {
			return true;
		}

		uint16_t value = 0;
		if (!readValue(process, memory_manager, source, value)) return false;
		bool ignored = false;
		return writeVariable(process, memory_manager, destination, value, ignored);
	}

	if (operation == "ADD" || operation == "SUBTRACT") {
		std::string destination;
		std::string left_operand;
		std::string right_operand;
		input >> destination >> left_operand >> right_operand;
		if (process->symbol_offsets.find(destination) ==
				process->symbol_offsets.end() &&
			process->symbol_offsets.size() >= MAX_SYMBOLS) {
			return true;
		}

		uint16_t left = 0;
		uint16_t right = 0;
		if (!readValue(process, memory_manager, left_operand, left) ||
			!readValue(process, memory_manager, right_operand, right)) {
			return false;
		}

		uint16_t result = 0;
		if (operation == "ADD") {
			result = clampUint16(static_cast<uint32_t>(left) + right);
		}
		else {
			result = left > right ? static_cast<uint16_t>(left - right) : 0;
		}

		bool ignored = false;
		return writeVariable(process, memory_manager, destination, result, ignored);
	}

	if (operation == "READ") {
		std::string destination;
		std::string address_text;
		input >> destination >> address_text;
		if (process->symbol_offsets.find(destination) ==
				process->symbol_offsets.end() &&
			process->symbol_offsets.size() >= MAX_SYMBOLS) {
			return true;
		}

		uint32_t address = 0;
		if (!parseAddress(address_text, address)) return false;
		uint16_t value = 0;
		if (!memory_manager->readMemory(process, address, value)) return false;

		bool ignored = false;
		return writeVariable(process, memory_manager, destination, value, ignored);
	}

	if (operation == "WRITE") {
		std::string address_text;
		std::string source;
		input >> address_text >> source;
		uint32_t address = 0;
		uint16_t value = 0;
		if (!parseAddress(address_text, address) ||
			!readValue(process, memory_manager, source, value)) {
			return false;
		}
		return memory_manager->writeMemory(process, address, value);
	}

	if (operation == "SLEEP") {
		uint64_t ticks = 0;
		std::string ticks_text;
		input >> ticks_text;
		if (!parseUnsigned64(ticks_text, ticks) ||
			ticks > std::numeric_limits<uint32_t>::max()) return false;
		process->sleep_ticks_remaining = static_cast<uint32_t>(ticks);
		return true;
	}

	return false;
}