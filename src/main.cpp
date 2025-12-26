#include <cstdlib>
#include <iostream>
#include <string>

#include <unistd.h>

static std::string GetCwd() {
  char* cwd = ::getcwd(nullptr, 0);
  if (!cwd) {
    return {};
  }
  std::string result(cwd);
  std::free(cwd);
  return result;
}

int main(int /*argc*/, char** /*argv*/) {
  const std::string cwd = GetCwd();
  if (cwd.empty()) {
    std::cerr << "Failed to get current working directory\n";
    return 1;
  }

  std::cout << "Current Directory: " << cwd << "\n";

  std::string line;
  while (true) {
    std::cout << "Enter command (type 'help' for all commands): ";
    if (!std::getline(std::cin, line)) {
      break;
    }

    if (line == "exit") {
      std::cout << "MiniFileExplorer closed successfully\n";
      break;
    }
  }

  return 0;
}

