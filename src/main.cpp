#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
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

static std::string ToLowerAscii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
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
  std::uintmax_t size_bytes = 0;
  std::time_t modify_time_t = 0;
  bool is_dir = false;
  bool is_empty_dir = false;
};

static std::uintmax_t CalculateDirectorySizeBytes(
    const std::filesystem::path& dir_path);

static void HandleLsCommand(const std::vector<std::string>& tokens) {
  enum class Mode {
    kNormal,
    kSortSize,
    kSortTime,
  };
  Mode mode = Mode::kNormal;
  if (tokens.size() == 2) {
    if (tokens[1] == "-s") {
      mode = Mode::kSortSize;
    } else if (tokens[1] == "-t") {
      mode = Mode::kSortTime;
    } else {
      std::cout << "Invalid option: ls\n";
      return;
    }
  } else if (tokens.size() != 1) {
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
    item.is_dir = is_dir;
    item.name = filename + (is_dir ? "/" : "");
    item.type = is_dir ? "Dir" : "File";

    if (mode == Mode::kSortSize && is_dir) {
      std::error_code empty_ec;
      item.is_empty_dir = fs::is_empty(path, empty_ec) && !empty_ec;
      item.size_bytes = CalculateDirectorySizeBytes(path);
      item.size = std::to_string(item.size_bytes);
    } else if (is_file) {
      std::error_code size_ec;
      const auto size_value = fs::file_size(path, size_ec);
      item.size_bytes = size_ec ? 0 : size_value;
      item.size = size_ec ? "-" : std::to_string(size_value);
    } else if (is_dir) {
      item.size = "-";
      item.size_bytes = 0;
    } else {
      item.size = "-";
      item.size_bytes = 0;
    }

    std::error_code time_ec;
    const auto ftime = fs::last_write_time(path, time_ec);
    item.modify_time_t = time_ec ? 0 : FileTimeToTimeT(ftime);
    item.modify_time = time_ec ? "-" : FormatLocalTime(item.modify_time_t);

    items.push_back(std::move(item));
  }

  if (mode == Mode::kSortTime) {
    std::sort(items.begin(), items.end(), [](const LsItem& a, const LsItem& b) {
      if (a.modify_time_t != b.modify_time_t) {
        return a.modify_time_t > b.modify_time_t;
      }
      return a.name < b.name;
    });
  } else if (mode == Mode::kSortSize) {
    std::sort(items.begin(), items.end(), [](const LsItem& a, const LsItem& b) {
      if (a.is_empty_dir != b.is_empty_dir) {
        return !a.is_empty_dir;
      }
      if (a.size_bytes != b.size_bytes) {
        return a.size_bytes > b.size_bytes;
      }
      return a.name < b.name;
    });
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

static void HandleTouchCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing filename: Please enter 'touch [name]'\n";
    return;
  }
  const std::string& name = tokens[1];

  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists(fs::path(name), ec) && !ec) {
    std::cout << "File already exists: " << name << "\n";
    return;
  }

  std::ofstream out(name, std::ios::out | std::ios::binary);
  if (!out) {
    std::cout << "Failed to create file: " << name << "\n";
    return;
  }
}

static void HandleMkdirCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing directory name: Please enter 'mkdir [name]'\n";
    return;
  }
  const std::string& name = tokens[1];

  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists(fs::path(name), ec) && !ec) {
    std::cout << "Directory already exists: " << name << "\n";
    return;
  }

  if (!fs::create_directory(fs::path(name), ec) || ec) {
    std::cout << "Failed to create directory: " << name << "\n";
    return;
  }
}

static void HandleRmCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing filename: Please enter 'rm [name]'\n";
    return;
  }
  const std::string& name = tokens[1];

  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path path(name);
  if (!fs::exists(path, ec) || ec) {
    std::cout << "File not found: " << name << "\n";
    return;
  }
  if (!fs::is_regular_file(path, ec) || ec) {
    std::cout << "Not a file: " << name << "\n";
    return;
  }

  std::cout << "Are you sure to delete " << name << "? (y/n)" << std::flush;
  std::string confirm;
  if (!std::getline(std::cin, confirm)) {
    return;
  }

  if (confirm != "y") {
    return;
  }

  if (!fs::remove(path, ec) || ec) {
    std::cout << "Failed to delete file: " << name << "\n";
  }
}

static void HandleRmdirCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing directory name: Please enter 'rmdir [name]'\n";
    return;
  }
  const std::string& name = tokens[1];

  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path path(name);
  if (!fs::exists(path, ec) || ec) {
    std::cout << "Directory not found: " << name << "\n";
    return;
  }
  if (!fs::is_directory(path, ec) || ec) {
    std::cout << "Not a directory: " << name << "\n";
    return;
  }
  if (!fs::is_empty(path, ec) || ec) {
    std::cout << "Directory not empty: " << name << "\n";
    return;
  }
  if (!fs::remove(path, ec) || ec) {
    std::cout << "Failed to delete directory: " << name << "\n";
  }
}

static std::time_t GetCreateTime(const struct stat& st) {
#if defined(__APPLE__)
  return st.st_birthtimespec.tv_sec;
#else
  return st.st_ctime;
#endif
}

static void HandleStatCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing target: Please enter'stat [name]'\n";
    return;
  }
  const std::string& name = tokens[1];

  struct stat st;
  if (::stat(name.c_str(), &st) != 0) {
    std::cout << "Target not found: " << name << "\n";
    return;
  }

  const bool is_dir = S_ISDIR(st.st_mode);
  const std::string type = is_dir ? "Dir" : "File";

  namespace fs = std::filesystem;
  std::error_code ec;
  std::string abs_path = fs::absolute(fs::path(name), ec).string();
  if (ec) {
    abs_path = name;
  }

  std::cout << "Type: " << type << "\n";
  std::cout << "Path: " << abs_path << "\n";
  std::cout << "Size: " << (is_dir ? "-" : std::to_string(st.st_size)) << "\n";
  std::cout << "Create Time: " << FormatLocalTime(GetCreateTime(st)) << "\n";
  std::cout << "Modify Time: " << FormatLocalTime(st.st_mtime) << "\n";
  std::cout << "Access Time: " << FormatLocalTime(st.st_atime) << "\n";
}

static void HandleSearchCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing keyword: Please enter 'search [keyword]'\n";
    return;
  }

  const std::string keyword = tokens[1];
  const std::string keyword_lower = ToLowerAscii(keyword);

  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path base = fs::current_path(ec);
  if (ec) {
    std::cout << "Failed to access current directory\n";
    return;
  }

  struct SearchResult {
    std::string path;
    std::string type;
  };
  std::vector<SearchResult> results;

  for (fs::recursive_directory_iterator it(
           base, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    const fs::path path = entry.path();
    const std::string filename = path.filename().string();
    if (ToLowerAscii(filename).find(keyword_lower) == std::string::npos) {
      continue;
    }

    std::error_code entry_ec;
    const bool is_dir = entry.is_directory(entry_ec);
    const bool is_file = entry.is_regular_file(entry_ec);

    std::error_code abs_ec;
    std::string abs_path = fs::absolute(path, abs_ec).string();
    if (abs_ec) {
      abs_path = path.string();
    }
    if (is_dir) {
      abs_path += "/";
    }

    const std::string type = is_dir ? "Dir" : (is_file ? "File" : "File");
    results.push_back(SearchResult{std::move(abs_path), type});
  }

  if (results.empty()) {
    std::cout << "No results found for '" << keyword << "'\n";
    return;
  }

  std::cout << "Search results for '" << keyword << "' (" << results.size()
            << " items):\n";
  for (const auto& r : results) {
    std::cout << r.path << " (" << r.type << ")\n";
  }
}

static void HandleCpCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 3) {
    std::cout << "Invalid target path\n";
    return;
  }

  namespace fs = std::filesystem;
  const fs::path src = fs::path(tokens[1]);
  const fs::path dst_arg = fs::path(tokens[2]);

  std::error_code ec;
  if (!fs::exists(src, ec) || ec || !fs::is_regular_file(src, ec) || ec) {
    std::cout << "Source not found\n";
    return;
  }

  fs::path dst_file = dst_arg;
  if (fs::exists(dst_arg, ec) && !ec && fs::is_directory(dst_arg, ec) && !ec) {
    dst_file = dst_arg / src.filename();
  }

  fs::path parent = dst_file.parent_path();
  if (parent.empty()) {
    parent = fs::path(".");
  }
  if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec) || ec) {
    std::cout << "Invalid target path\n";
    return;
  }
  if (fs::exists(dst_file, ec) && !ec && fs::is_directory(dst_file, ec) && !ec) {
    std::cout << "Invalid target path\n";
    return;
  }

  fs::copy_options options = fs::copy_options::none;
  if (fs::exists(dst_file, ec) && !ec) {
    std::cout << "File exists in target: Overwrite? (y/n)" << std::flush;
    std::string confirm;
    if (!std::getline(std::cin, confirm)) {
      return;
    }
    if (confirm != "y") {
      return;
    }
    options = fs::copy_options::overwrite_existing;
  }

  if (!fs::copy_file(src, dst_file, options, ec) || ec) {
    std::cout << "Invalid target path\n";
  }
}

static void HandleMvCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 3) {
    std::cout << "Invalid target path\n";
    return;
  }

  namespace fs = std::filesystem;
  const fs::path src = fs::path(tokens[1]);
  const fs::path dst_arg = fs::path(tokens[2]);

  std::error_code ec;
  if (!fs::exists(src, ec) || ec) {
    std::cout << "Source not found\n";
    return;
  }

  fs::path dst_final = dst_arg;
  if (fs::exists(dst_arg, ec) && !ec && fs::is_directory(dst_arg, ec) && !ec) {
    dst_final = dst_arg / src.filename();
  }

  fs::path parent = dst_final.parent_path();
  if (parent.empty()) {
    parent = fs::path(".");
  }
  if (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec) || ec) {
    std::cout << "Invalid target path\n";
    return;
  }
  if (fs::exists(dst_final, ec) && !ec) {
    std::cout << "Invalid target path\n";
    return;
  }

  fs::rename(src, dst_final, ec);
  if (!ec) {
    return;
  }

  if (fs::is_regular_file(src, ec) && !ec) {
    std::error_code copy_ec;
    if (!fs::copy_file(src, dst_final, fs::copy_options::none, copy_ec) || copy_ec) {
      std::cout << "Invalid target path\n";
      return;
    }
    std::error_code remove_ec;
    if (!fs::remove(src, remove_ec) || remove_ec) {
      std::cout << "Invalid target path\n";
      return;
    }
    return;
  }

  std::cout << "Invalid target path\n";
}

static std::uintmax_t CalculateDirectorySizeBytes(
    const std::filesystem::path& dir_path) {
  namespace fs = std::filesystem;
  std::uintmax_t total = 0;

  std::error_code ec;
  for (fs::recursive_directory_iterator it(
           dir_path, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec) {
      continue;
    }
    std::error_code size_ec;
    const auto size_value = fs::file_size(entry.path(), size_ec);
    if (!size_ec) {
      total += size_value;
    }
  }

  return total;
}

static void HandleDuCommand(const std::vector<std::string>& tokens) {
  if (tokens.size() < 2) {
    std::cout << "Missing directory name: Please enter 'du [name]'\n";
    return;
  }

  const std::string& arg = tokens[1];
  namespace fs = std::filesystem;
  const fs::path dir_path(arg);

  std::error_code ec;
  if (!fs::exists(dir_path, ec) || ec || !fs::is_directory(dir_path, ec) || ec) {
    std::cout << "Invalid directory: " << arg << "\n";
    return;
  }

  const std::uintmax_t bytes = CalculateDirectorySizeBytes(dir_path);
  const std::uintmax_t kb = 1024;
  const std::uintmax_t mb = 1024 * 1024;
  if (bytes >= mb) {
    const std::uintmax_t value = (bytes + (mb / 2)) / mb;
    std::cout << "Total size of " << arg << ": " << value << " MB\n";
    return;
  }
  const std::uintmax_t value = (bytes + (kb / 2)) / kb;
  std::cout << "Total size of " << arg << ": " << value << " KB\n";
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
    if (cmd == "touch") {
      HandleTouchCommand(tokens);
      continue;
    }
    if (cmd == "mkdir") {
      HandleMkdirCommand(tokens);
      continue;
    }
    if (cmd == "rm") {
      HandleRmCommand(tokens);
      continue;
    }
    if (cmd == "rmdir") {
      HandleRmdirCommand(tokens);
      continue;
    }
    if (cmd == "stat") {
      HandleStatCommand(tokens);
      continue;
    }
    if (cmd == "search") {
      HandleSearchCommand(tokens);
      continue;
    }
    if (cmd == "cp") {
      HandleCpCommand(tokens);
      continue;
    }
    if (cmd == "mv") {
      HandleMvCommand(tokens);
      continue;
    }
    if (cmd == "du") {
      HandleDuCommand(tokens);
      continue;
    }

    std::cout << "Unknown command: " << cmd << "\n";
  }

  return 0;
}
