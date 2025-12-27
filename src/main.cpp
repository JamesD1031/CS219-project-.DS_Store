#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
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

static std::time_t FileTimeToTimeT(std::filesystem::file_time_type file_time) {
  using namespace std::chrono;
  const auto system_time =
      time_point_cast<system_clock::duration>(file_time -
                                             std::filesystem::file_time_type::clock::now() +
                                             system_clock::now());
  return system_clock::to_time_t(system_time);
}

static std::string FormatLocalTime(std::time_t time_value) {
  std::tm tm{};
  if (::localtime_r(&time_value, &tm) == nullptr) {
    return "-";
  }
  char buf[20];
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
    return "-";
  }
  return std::string(buf);
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

struct LsItem {
  std::string name;
  std::string type;
  std::string size;
  std::string modify_time;
};

static void HandleLsCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() != 1) {
    std::cout << "Invalid option: ls\n";
    return;
  }

  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path dir = fs::current_path(ec);
  if (ec) {
    std::cout << "Failed to access current directory\n";
    return;
  }

  std::vector<LsItem> items;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    const fs::path path = entry.path();
    const std::string filename = path.filename().string();

    std::error_code entry_ec;
    const bool is_dir = entry.is_directory(entry_ec);
    const bool is_file = entry.is_regular_file(entry_ec);

    LsItem item;
    item.name = filename + (is_dir ? "/" : "");
    item.type = is_dir ? "Dir" : "File";

    if (is_dir) {
      item.size = "-";
    } else if (is_file) {
      std::error_code size_ec;
      const auto size_value = fs::file_size(path, size_ec);
      item.size = size_ec ? "-" : std::to_string(size_value);
    } else {
      item.size = "-";
    }

    std::error_code time_ec;
    const auto ftime = fs::last_write_time(path, time_ec);
    item.modify_time = time_ec ? "-" : FormatLocalTime(FileTimeToTimeT(ftime));

    items.push_back(std::move(item));
  }

  size_t name_w = std::string("Name").size();
  size_t type_w = std::string("Type").size();
  size_t size_w = std::string("Size(B)").size();
  for (const auto& item : items) {
    name_w = std::max(name_w, item.name.size());
    type_w = std::max(type_w, item.type.size());
    size_w = std::max(size_w, item.size.size());
  }

  std::cout << std::left << std::setw(static_cast<int>(name_w)) << "Name"
            << " " << std::left << std::setw(static_cast<int>(type_w)) << "Type"
            << " " << std::right << std::setw(static_cast<int>(size_w)) << "Size(B)"
            << " " << "Modify Time" << "\n";

  for (const auto& item : items) {
    std::cout << std::left << std::setw(static_cast<int>(name_w)) << item.name
              << " " << std::left << std::setw(static_cast<int>(type_w)) << item.type
              << " " << std::right << std::setw(static_cast<int>(size_w)) << item.size
              << " " << item.modify_time << "\n";
  }
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
    if (cmd == "ls") {
      HandleLsCommand(tokens);
      continue;
    }

    std::cout << "Unknown command: " << cmd << "\n";
  }

  return 0;
}
