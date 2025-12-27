#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>
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

static std::string GetHomeDir() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home);
  }
  passwd* pw = ::getpwuid(::getuid());
  if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0') {
    return std::string(pw->pw_dir);
  }
  return {};
}

static void PrintHelp() {
  std::cout << "Supported commands:\n";
  std::cout << "  cd [path]: Switch to target directory\n";
  std::cout << "  cd ~: Switch to home directory\n";
  std::cout << "  ls: List all files and directories\n";
  std::cout << "  ls -s: List and sort by size (desc)\n";
  std::cout << "  ls -t: List and sort by modify time (desc)\n";
  std::cout << "  touch [file]: Create an empty file\n";
  std::cout << "  mkdir [dir]: Create an empty directory\n";
  std::cout << "  rm [file]: Delete a file (with confirmation)\n";
  std::cout << "  rmdir [dir]: Delete an empty directory\n";
  std::cout << "  stat [name]: Show detailed information\n";
  std::cout << "  search [keyword]: Search files and directories recursively\n";
  std::cout << "  cp [src] [dst]: Copy a file\n";
  std::cout << "  mv [src] [dst]: Move/rename a file or directory\n";
  std::cout << "  du [dir]: Calculate total directory size\n";
  std::cout << "  help: Show all commands\n";
  std::cout << "  exit: Exit the program\n";
}

static std::optional<std::vector<std::string>> TokenizeCommandLine(
    const std::string& line) {
  std::vector<std::string> tokens;
  std::string current;

  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escape_next = false;

  auto flush = [&]() {
    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  };

  for (char ch : line) {
    if (escape_next) {
      current.push_back(ch);
      escape_next = false;
      continue;
    }

    if (ch == '\\') {
      escape_next = true;
      continue;
    }

    if (in_single_quote) {
      if (ch == '\'') {
        in_single_quote = false;
      } else {
        current.push_back(ch);
      }
      continue;
    }

    if (in_double_quote) {
      if (ch == '"') {
        in_double_quote = false;
      } else {
        current.push_back(ch);
      }
      continue;
    }

    if (ch == '\'') {
      in_single_quote = true;
      continue;
    }
    if (ch == '"') {
      in_double_quote = true;
      continue;
    }

    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      flush();
      continue;
    }
    current.push_back(ch);
  }

  if (escape_next || in_single_quote || in_double_quote) {
    return std::nullopt;
  }
  flush();
  return tokens;
}

static void HandleCdCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing path: Please enter 'cd [path]'\n";
    return;
  }

  const std::string& arg = tokens[1];
  std::string target = arg;
  if (arg == "~") {
    target = GetHomeDir();
  }

  struct stat st;
  if (target.empty() || ::stat(target.c_str(), &st) != 0) {
    std::cout << "Invalid directory: " << arg << "\n";
    return;
  }
  if (!S_ISDIR(st.st_mode)) {
    std::cout << "Not a directory: " << arg << "\n";
    return;
  }
  if (::chdir(target.c_str()) != 0) {
    std::cout << "Invalid directory: " << arg << "\n";
    return;
  }
}

int main(int argc, char** argv) {
  if (argc >= 2) {
    const std::string initial_dir = argv[1];
    struct stat st;
    if (::stat(initial_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) ||
        ::chdir(initial_dir.c_str()) != 0) {
      std::cout << "Directory not found: " << initial_dir << "\n";
      return 1;
    }
  }

  const std::string cwd = GetCwd();
  if (cwd.empty()) {
    std::cerr << "Failed to get current working directory\n";
    return 1;
  }

  std::cout << "Current Directory: " << cwd << "\n";

  std::string line;
  while (true) {
    std::cout << "Enter command (type 'help' for all commands): " << std::flush;
    if (!std::getline(std::cin, line)) {
      break;
    }

    const auto tokens_or = TokenizeCommandLine(line);
    if (!tokens_or.has_value()) {
      std::cout << "Invalid command: unmatched quote\n";
      continue;
    }
    const std::vector<std::string>& tokens = *tokens_or;
    if (tokens.empty()) {
      continue;
    }

    const std::string& cmd = tokens[0];
    if (cmd == "exit") {
      std::cout << "MiniFileExplorer closed successfully\n";
      break;
    }
    if (cmd == "help") {
      PrintHelp();
      continue;
    }
    if (cmd == "cd") {
      HandleCdCommand(tokens);
      continue;
    }

    std::cout << "Unknown command: " << cmd << "\n";
  }

  return 0;
}
