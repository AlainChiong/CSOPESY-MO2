#ifndef CONSOLE_MANAGER_H
#define CONSOLE_MANAGER_H

#include <memory>
#include <string>

class MemoryManager;

class ConsoleManager {
public:
	static ConsoleManager& getInstance();
	void startConsole();

private:
	ConsoleManager() = default;
	~ConsoleManager();

	ConsoleManager(const ConsoleManager&) = delete;
	ConsoleManager& operator=(const ConsoleManager&) = delete;

	void displayMainMenu() const;
	void processCommand(const std::string& command);
	void processScreenCommand(const std::string& command);
	void initialize();

	std::unique_ptr<MemoryManager> memory_manager;
};

#endif