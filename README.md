# MCP Server

这是一个使用 C++17 实现的 MCP Server 示例项目，当前支持 HTTP JSON-RPC、stdio JSON-RPC、SSE 工具调用事件、线程池调度、SQLite 工具调用历史记录，以及内置 Tools / Resources / Prompts。

项目目标是把 MCP 协议层、JSON-RPC 传输层、业务工具注册和后续扩展点拆开，方便继续加入更多工具、Provider、数据库能力和客户端示例。

## 功能概览

- MCP 初始化、Tools、Resources、Prompts 基础协议类型
- HTTP JSON-RPC endpoint：`/jsonrpc`
- stdio JSON-RPC server
- SSE endpoint：
  - `/sse/events`：服务状态流
  - `/sse/tool_calls`：工具调用开始、完成、错误事件
- 可配置线程池 dispatcher
- SQLite 工具调用历史记录
- 内置工具：
  - `echo`
  - `calculate`
  - `get_time`
  - `get_weather`
  - `write_file`
  - `generate_image`
- Python + Ollama MCP demo client
- GoogleTest 单元测试

## 项目结构

```text
.
├── CMakeLists.txt
├── conanfile.txt
├── config/
│   └── server.json
├── mcp/
│   ├── mcp_types.*
│   └── mcp_server.*
├── src/
│   ├── main/
│   │   ├── mcp_server_main.cpp
│   │   ├── server_app.*
│   │   ├── mcp_builtin_tools.*
│   │   ├── mcp_builtin_resources.*
│   │   └── mcp_builtin_prompts.*
│   ├── json_rpc/
│   │   ├── jsonrpc_dispatcher.*
│   │   ├── jsonrpc_serialization.*
│   │   ├── http_jsonrpc.*
│   │   └── stdio_jsonrpc_server.*
│   ├── config/
│   │   └── config.*
│   ├── logger/
│   │   └── logger.*
│   ├── sql/
│   │   ├── sqlite_database.*
│   │   └── tool_call_history_repository.*
│   └── utils/
│       └── thread_pool.*
├── client/
│   └── ollama_mcp_demo.py
└── tests/
    ├── config_test.cpp
    ├── jsonrpc_test.cpp
    ├── http_jsonrpc_test.cpp
    ├── logger_test.cpp
    ├── mcp_server_test.cpp
    ├── mcp_types_test.cpp
    └── thread_pool_test.cpp
```

核心分层：

- `mcp/`：MCP 协议对象、Server 注册中心、工具/资源/提示词调用入口。
- `src/json_rpc/`：JSON-RPC 请求响应类型、序列化、dispatcher、HTTP/stdio 传输。
- `src/main/`：程序入口、Server 装配、内置 Tools / Resources / Prompts 注册。
- `src/sql/`：SQLite 封装和工具调用历史仓库。
- `src/config/`：读取并校验 `config/server.json`。
- `src/utils/`：线程池。

## 依赖

项目使用 Conan 管理第三方依赖：

- `spdlog`
- `nlohmann_json`
- `cpp-httplib`
- `libcurl`
- `gtest`
- `sqlite3`

当前 CMake 配置使用 C++17，并在 `CMakeLists.txt` 中设置了 `CMAKE_OSX_ARCHITECTURES arm64`，主要面向 Apple Silicon 环境。如果在其他架构构建，需要按本机环境调整。

## 构建

推荐使用当前项目的 Debug 构建目录：

```bash
conan install . --output-folder=cmake-build-debug --build=missing -s build_type=Debug
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

运行测试：

```bash
ctest --test-dir cmake-build-debug --output-on-failure
```

## 配置

主配置文件为 `config/server.json`：

```json
{
  "server": {
    "port": 8080
  },
  "logging": {
    "log_file_path": "../logs/server.log",
    "log_level": "debug",
    "log_file_size": 52428800,
    "log_file_count": 5,
    "log_console_output": true
  },
  "thread_pool": {
    "size": 16,
    "max_queue_size": 128,
    "pooled_methods": [
      "tools/call",
      "resources/read",
      "prompts/get"
    ]
  },
  "image_generation": {
    "default_provider": "doubao",
    "doubao": {
      "api_key": "YOUR_DOUBAO_ARK_API_KEY",
      "model": "doubao-seedream-4-5-251128",
      "api_url": "https://ark.cn-beijing.volces.com/api/v3/images/generations"
    },
    "gemini": {
      "api_key": "",
      "model": "gemini-3.1-flash-image-preview"
    }
  }
}
```

说明：

- `server.port`：HTTP 服务端口。
- `logging`：日志文件、等级、轮转大小和控制台输出。
- `thread_pool.size`：线程池工作线程数量。
- `thread_pool.max_queue_size`：线程池最大排队任务数。
- `thread_pool.pooled_methods`：进入线程池执行的 JSON-RPC 方法。
- `image_generation.default_provider`：`generate_image` 默认使用的生图 provider。
- `image_generation.doubao`：豆包 Ark 生图 API 配置。

如果要上传 GitHub，不要提交真实 API Key。即使 `.gitignore` 已经写了 `/config/server.json`，如果该文件之前已经被 Git 跟踪，还需要执行：

```bash
git rm --cached config/server.json
```

之后保留本地 `config/server.json` 自用，仓库里建议只放不含密钥的示例配置。

## 运行

HTTP 模式：

```bash
./cmake-build-debug/MCP_Server --mode http --config config/server.json --port 8080
```

stdio 模式：

```bash
./cmake-build-debug/MCP_Server --mode stdio --config config/server.json
```

同时启动 HTTP 和 stdio：

```bash
./cmake-build-debug/MCP_Server --mode both --config config/server.json --port 8080
```

查看参数：

```bash
./cmake-build-debug/MCP_Server --help
```

## JSON-RPC 示例

初始化：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {
        "name": "demo-client",
        "version": "0.1.0"
      }
    }
  }'
```

列出工具：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list",
    "params": {}
  }'
```

调用工具：

```bash
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "calculate",
      "arguments": {
        "operation": "add",
        "a": 10,
        "b": 20
      }
    }
  }'
```

## 内置 Tools

| Tool | 参数 | 说明 |
| --- | --- | --- |
| `echo` | `message` | 返回输入文本 |
| `calculate` | `operation`, `a`, `b` | 基础四则运算和取模 |
| `get_time` | 无 | 获取当前系统时间 |
| `get_weather` | `city` | 通过 Open-Meteo 获取城市天气 |
| `write_file` | `path`, `content` | 写入本地文件 |
| `generate_image` | `prompt`, `filename`, `size?`, `provider?` | 调用生图 API，解码 base64 图片并保存到 `generated/images` |

`generate_image` 的提示词由客户端模型提供，服务端只负责：

1. 读取 provider 配置；
2. 发送生图请求；
3. 从 API 响应中提取 base64 图片；
4. 解码为二进制图片数据；
5. 保存到本地 `generated/images`。

## 内置 Resources

| Resource | 说明 |
| --- | --- |
| `system://info` | 返回基础系统信息 |
| `config://server` | 返回部分服务配置 |

## 内置 Prompts

| Prompt | 参数 | 说明 |
| --- | --- | --- |
| `code_review` | `code`, `language` | 生成代码审查提示词 |

## SQLite 工具调用历史

服务启动时会初始化工具调用历史仓库，默认数据库路径：

```text
data/tool_call_history.sqlite3
```

每次工具调用会记录：

- 工具名称
- 参数 JSON
- 是否错误
- 返回结果 JSON
- 错误信息
- 开始时间
- 结束时间
- 耗时毫秒数

SQLite 写入失败不会中断工具调用，只会写入 warn 日志。

## Python + Ollama 客户端

示例客户端在：

```text
client/ollama_mcp_demo.py
```

使用前需要：

1. 启动 MCP Server HTTP 模式；
2. 启动 Ollama；
3. 拉取并确认脚本中配置的模型名称存在。

示例：

```bash
ollama serve
ollama pull qwen3.5:4b
python3 client/ollama_mcp_demo.py
```

如果出现 `localhost:11434 connection refused`，通常是 Ollama 服务没有启动，或本机没有监听 `11434` 端口。

## 后续优化方向

- 把 `config/server.json` 拆成可提交的 example 配置和本地私有配置。
- 给 `generate_image` 抽象 provider 接口，进一步拆分豆包、Gemini 等实现。
- 增加工具调用历史查询工具，例如 `list_tool_history`。
- 为 `write_file` 增加路径白名单，避免任意路径写入。
- 给 HTTP Server 增加鉴权或本地访问限制。
- 增加数据库迁移版本表，方便后续 schema 演进。
