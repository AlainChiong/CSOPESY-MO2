#include "MemoryManager.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
	int hexDigitValue(char character) {
		if (character >= '0' && character <= '9') return character - '0';
		if (character >= 'a' && character <= 'f') return character - 'a' + 10;
		if (character >= 'A' && character <= 'F') return character - 'A' + 10;
		return -1;
	}
}

MemoryManager::MemoryManager(uint32_t max_mem, uint32_t frame_size)
	: total_memory(max_mem),
	  mem_per_frame(frame_size),
	  free_memory(max_mem),
	  used_memory(0),
	  num_paged_in(0),
	  num_paged_out(0) {
	if (frame_size == 0 || max_mem < frame_size || max_mem % frame_size != 0) {
		throw std::invalid_argument(
			"Physical memory must contain a whole, non-zero number of frames.");
	}

	const uint32_t frame_count = max_mem / frame_size;
	physical_memory.reserve(frame_count);
	for (uint32_t index = 0; index < frame_count; ++index) {
		physical_memory.emplace_back(static_cast<int>(index), frame_size);
	}

	// Each emulator run starts with an empty backing store.
	std::ofstream store(backing_store_path, std::ios::trunc);
}

bool MemoryManager::isPowerOfTwo(uint32_t value) {
	return value != 0 && (value & (value - 1)) == 0;
}

bool MemoryManager::allocateMemory(Process* process) {
	if (process == nullptr) return false;

	std::lock_guard<std::mutex> lock(memory_mutex);
	if (process->memory_allocated || process->memory_size < 64 ||
		process->memory_size > 65536 || !isPowerOfTwo(process->memory_size)) {
		return false;
	}

	const uint32_t page_count =
		(process->memory_size + mem_per_frame - 1) / mem_per_frame;
	process->num_pages = page_count;
	process->page_table.assign(page_count, -1);
	process->memory_allocated = true;
	return true;
}

void MemoryManager::deallocateMemory(Process* process) {
	if (process == nullptr) return;

	std::lock_guard<std::mutex> lock(memory_mutex);
	if (!process->memory_allocated) return;

	for (Frame& frame : physical_memory) {
		if (frame.is_free || frame.owner != process) continue;

		removeFrameFromQueue(frame.frame_index);
		frame.is_free = true;
		frame.owner = nullptr;
		frame.page_index = -1;
		std::fill(frame.data.begin(), frame.data.end(), 0);
	}

	std::fill(process->page_table.begin(), process->page_table.end(), -1);
	process->memory_allocated = false;
	removeProcessFromStore(process);

	uint32_t occupied_frames = 0;
	for (const Frame& frame : physical_memory) {
		if (!frame.is_free) occupied_frames++;
	}
	used_memory = occupied_frames * mem_per_frame;
	free_memory = total_memory - used_memory;
}

int MemoryManager::ensurePageLoaded(Process* process, uint32_t page_index) {
	if (page_index >= process->page_table.size()) return -1;

	int frame_index = process->page_table[page_index];
	if (frame_index >= 0 &&
		static_cast<size_t>(frame_index) < physical_memory.size()) {
		const Frame& frame = physical_memory[frame_index];
		if (!frame.is_free && frame.owner == process &&
			frame.page_index == static_cast<int>(page_index)) {
			return frame_index;
		}

		process->page_table[page_index] = -1;
	}

	return handlePageFault(process, page_index);
}

int MemoryManager::handlePageFault(Process* process, uint32_t page_index) {
	int target_frame = -1;

	for (const Frame& frame : physical_memory) {
		if (frame.is_free) {
			target_frame = frame.frame_index;
			break;
		}
	}

	if (target_frame == -1) {
		while (!frame_allocation_queue.empty()) {
			const int candidate = frame_allocation_queue.front();
			frame_allocation_queue.pop_front();
			if (candidate >= 0 &&
				static_cast<size_t>(candidate) < physical_memory.size() &&
				!physical_memory[candidate].is_free) {
				target_frame = candidate;
				break;
			}
		}

		// Defensive fallback: the deque should contain every occupied frame.
		if (target_frame == -1) {
			for (const Frame& frame : physical_memory) {
				if (!frame.is_free) {
					target_frame = frame.frame_index;
					break;
				}
			}
		}

		if (target_frame == -1) return -1;
		evictPage(target_frame);
	}

	removeFrameFromQueue(target_frame);
	loadPageFromStore(process, page_index, target_frame);
	process->page_table[page_index] = target_frame;
	frame_allocation_queue.push_back(target_frame);
	return target_frame;
}

void MemoryManager::evictPage(int frame_index) {
	if (frame_index < 0 ||
		static_cast<size_t>(frame_index) >= physical_memory.size()) {
		return;
	}

	Frame& frame = physical_memory[frame_index];
	if (frame.is_free || frame.owner == nullptr) return;

	Process* victim = frame.owner;
	savePageToStore(victim, static_cast<uint32_t>(frame.page_index), frame.data);

	if (frame.page_index >= 0 &&
		static_cast<size_t>(frame.page_index) < victim->page_table.size() &&
		victim->page_table[frame.page_index] == frame_index) {
		victim->page_table[frame.page_index] = -1;
	}

	num_paged_out++;
	frame.is_free = true;
	frame.owner = nullptr;
	frame.page_index = -1;
	if (used_memory >= mem_per_frame) used_memory -= mem_per_frame;
	free_memory += mem_per_frame;
}

void MemoryManager::loadPageFromStore(Process* process, uint32_t page_index, int frame_index) {
	Frame& frame = physical_memory[frame_index];
	frame.is_free = false;
	frame.owner = process;
	frame.page_index = static_cast<int>(page_index);
	std::fill(frame.data.begin(), frame.data.end(), 0);

	std::ifstream store(backing_store_path);
	std::string line;
	std::string latest_hex_data;
	while (std::getline(store, line)) {
		std::istringstream input(line);
		int stored_id = -1;
		std::string stored_name;
		uint32_t stored_page = 0;
		std::string hex_data;
		if (input >> stored_id >> stored_name >> stored_page >> hex_data &&
			stored_id == process->id && stored_name == process->name &&
			stored_page == page_index) {
			latest_hex_data = hex_data;
		}
	}

	const size_t bytes_to_restore = std::min(frame.data.size(),
		latest_hex_data.size() / 2);
	for (size_t index = 0; index < bytes_to_restore; ++index) {
		const int high = hexDigitValue(latest_hex_data[index * 2]);
		const int low = hexDigitValue(latest_hex_data[index * 2 + 1]);
		if (high < 0 || low < 0) break;
		frame.data[index] = static_cast<uint8_t>((high << 4) | low);
	}

	num_paged_in++;
	if (free_memory >= mem_per_frame) free_memory -= mem_per_frame;
	used_memory += mem_per_frame;
}

void MemoryManager::savePageToStore(const Process* process, uint32_t page_index, const std::vector<uint8_t>& data) {
	std::ofstream store(backing_store_path, std::ios::app);
	if (!store.is_open()) return;

	store << process->id << ' ' << process->name << ' ' << page_index << ' ';
	for (uint8_t byte : data) {
		store << std::hex << std::setw(2) << std::setfill('0')
			<< static_cast<unsigned int>(byte);
	}
	store << std::dec << '\n';
}

void MemoryManager::removeProcessFromStore(const Process* process) {
	std::ifstream store(backing_store_path);
	if (!store.is_open()) return;

	std::vector<std::string> retained_lines;
	std::string line;
	while (std::getline(store, line)) {
		std::istringstream input(line);
		int stored_id = -1;
		std::string stored_name;
		uint32_t stored_page = 0;
		std::string hex_data;
		const bool parsed = static_cast<bool>(
			input >> stored_id >> stored_name >> stored_page >> hex_data);
		if (!parsed || stored_id != process->id || stored_name != process->name) {
			retained_lines.push_back(line);
		}
	}
	store.close();

	std::ofstream rewritten(backing_store_path, std::ios::trunc);
	for (const std::string& retained : retained_lines) {
		rewritten << retained << '\n';
	}
}

void MemoryManager::removeFrameFromQueue(int frame_index) {
	frame_allocation_queue.erase(
		std::remove(frame_allocation_queue.begin(), frame_allocation_queue.end(),
			frame_index),
		frame_allocation_queue.end());
}

void MemoryManager::recordAccessViolation(Process* process, uint32_t address) const {
	process->access_violation = true;
	process->invalid_address = address;

	const std::time_t now = std::time(nullptr);
	std::tm local_time{};
#ifdef _WIN32
	localtime_s(&local_time, &now);
#else
	localtime_r(&now, &local_time);
#endif

	char time_text[9]{};
	std::strftime(time_text, sizeof(time_text), "%H:%M:%S", &local_time);
	process->violation_time = time_text;
}

bool MemoryManager::readMemory(Process* process, uint32_t address, uint16_t& out_value) {
	if (process == nullptr) return false;

	std::lock_guard<std::mutex> lock(memory_mutex);
	if (!process->memory_allocated) return false;
	if (process->memory_size < sizeof(uint16_t) ||
		address > process->memory_size - sizeof(uint16_t)) {
		recordAccessViolation(process, address);
		return false;
	}

	const uint32_t first_page = address / mem_per_frame;
	const uint32_t first_offset = address % mem_per_frame;
	const int first_frame = ensurePageLoaded(process, first_page);
	if (first_frame < 0) return false;

	const uint8_t low_byte = physical_memory[first_frame].data[first_offset];
	uint8_t high_byte = 0;
	if (first_offset + 1 < mem_per_frame) {
		high_byte = physical_memory[first_frame].data[first_offset + 1];
	}
	else {
		const int second_frame = ensurePageLoaded(process, first_page + 1);
		if (second_frame < 0) return false;
		high_byte = physical_memory[second_frame].data[0];
	}

	out_value = static_cast<uint16_t>(
		static_cast<uint16_t>(high_byte) << 8 | low_byte);
	return true;
}

bool MemoryManager::writeMemory(Process* process, uint32_t address, uint16_t value) {
	if (process == nullptr) return false;

	std::lock_guard<std::mutex> lock(memory_mutex);
	if (!process->memory_allocated) return false;
	if (process->memory_size < sizeof(uint16_t) ||
		address > process->memory_size - sizeof(uint16_t)) {
		recordAccessViolation(process, address);
		return false;
	}

	const uint32_t first_page = address / mem_per_frame;
	const uint32_t first_offset = address % mem_per_frame;
	const int first_frame = ensurePageLoaded(process, first_page);
	if (first_frame < 0) return false;

	physical_memory[first_frame].data[first_offset] =
		static_cast<uint8_t>(value & 0xFF);
	if (first_offset + 1 < mem_per_frame) {
		physical_memory[first_frame].data[first_offset + 1] =
			static_cast<uint8_t>((value >> 8) & 0xFF);
	}
	else {
		const int second_frame = ensurePageLoaded(process, first_page + 1);
		if (second_frame < 0) return false;
		physical_memory[second_frame].data[0] =
			static_cast<uint8_t>((value >> 8) & 0xFF);
	}

	return true;
}

uint32_t MemoryManager::getTotalMemory() const {
	std::lock_guard<std::mutex> lock(memory_mutex);
	return total_memory;
}

uint32_t MemoryManager::getUsedMemory() const {
	std::lock_guard<std::mutex> lock(memory_mutex);
	return used_memory;
}

uint32_t MemoryManager::getFreeMemory() const {
	std::lock_guard<std::mutex> lock(memory_mutex);
	return free_memory;
}

uint32_t MemoryManager::getPagedIn() const {
	std::lock_guard<std::mutex> lock(memory_mutex);
	return num_paged_in;
}

uint32_t MemoryManager::getPagedOut() const {
	std::lock_guard<std::mutex> lock(memory_mutex);
	return num_paged_out;
}

uint32_t MemoryManager::getResidentMemory(const Process* process) const {
	if (process == nullptr) return 0;

	std::lock_guard<std::mutex> lock(memory_mutex);
	uint32_t resident_frames = 0;
	for (const Frame& frame : physical_memory) {
		if (!frame.is_free && frame.owner == process) resident_frames++;
	}
	return resident_frames * mem_per_frame;
}