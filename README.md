# .DS_Store (CS219 proj4)

基于终端的迷你文件管理器（C++17），支持目录切换、文件/目录创建删除、列表展示、信息查询，以及进阶的搜索/复制移动/目录大小与排序。

## Build

在仓库根目录执行：

```bash
make
```

生成可执行文件：`build/MiniFileExplorer`

## Run

```bash
./build/MiniFileExplorer [initial_directory]
```

- 不带参数：默认使用当前工作目录（`getcwd()`）并打印 `Current Directory: ...`
- 带参数：使用指定目录作为初始目录；若目录不存在则打印 `Directory not found: ...` 并退出

启动后进入交互模式，提示：

`Enter command (type 'help' for all commands): `

## Commands

- `help`：列出全部命令
- `exit`：退出并打印 `MiniFileExplorer closed successfully`

### Directory

- `cd [path]`：切换目录（支持相对路径，如 `cd ../..`、`cd ./test`）
  - 不存在：`Invalid directory: [path]`
  - 是文件非目录：`Not a directory: [path]`
- `cd ~`：切换到当前用户主目录

### List

- `ls`：列出当前目录下内容，4 列对齐：Name / Type / Size(B) / Modify Time
- `ls -s`：按大小降序排序（目录按子文件总大小计算，空目录排在最后）
- `ls -t`：按修改时间降序排序

### Create / Delete

- `touch [file]`：创建空文件；已存在：`File already exists: [file]`
- `mkdir [dir]`：创建空目录；已存在：`Directory already exists: [dir]`
- `rm [file]`：删除文件（二次确认）
  - 确认提示：`Are you sure to delete [file]? (y/n)`
  - 仅输入 `y` 才会删除
- `rmdir [dir]`：删除空目录
  - 非空：`Directory not empty: [dir]`
  - 不存在：`Directory not found: [dir]`

### Query

- `stat [name]`：显示类型/路径/大小/创建时间/修改时间/访问时间
  - 缺参：`Missing target: Please enter'stat [name]'`
  - 不存在：`Target not found: [name]`

### Advanced

- `search [keyword]`：递归搜索当前目录及子目录（不区分大小写）
  - 有结果：`Search results for '[keyword]' (N items):` + 列表
  - 无结果：`No results found for '[keyword]'`
- `cp [src] [dst]`：复制文件；目标存在同名文件时提示覆盖确认
  - 覆盖提示：`File exists in target: Overwrite? (y/n)`
  - 源不存在：`Source not found`
  - 目标非法：`Invalid target path`
- `mv [src] [dst]`：移动/重命名文件或目录
  - 源不存在：`Source not found`
  - 目标非法：`Invalid target path`
- `du [dir]`：计算目录总大小（自动换算 KB/MB）
  - 输出：`Total size of [dir]: N KB/MB`
 

### Smoke 测试（Shell 脚本）

这是一个用于快速验证 `MiniFileExplorer` 基本功能的简单 Smoke 测试脚本，无需手敲命令。

运行方法：

```bash
./scripts/smoke_test.sh



## Example

```bash
make
./build/MiniFileExplorer .
```

在交互中输入：

```text
mkdir data
touch note.txt
ls
stat note.txt
search note
exit
```

