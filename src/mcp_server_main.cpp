#include <iostream>
#include "config/config.h"
#include "logger/logger.h"
#include "json_rpc/stdio_jsonrpc_server.h"
#include "json_rpc/jsonrpc_serialization.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <memory>
#include <ctime>
#include <fstream>
#include <sstream>
#include <cctype>
#include <iomanip>
#include <stdexcept>
#include <string>

#include <curl/curl.h>

#include "json_rpc/http_jsonrpc.h"
#include "mcp/mcp_server.h"

using namespace mcp;

static std::atomic<bool> g_running{true};
static std::unique_ptr<jsonrpc::HttpJsonRpcServer> g_http_server{nullptr};

void SignalHandler(int signal) {
    std::cerr << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    if (g_http_server) {
        g_http_server->Stop();
    }
}
namespace {
    ToolResult MakeTextResult(const std::string& text, bool is_error = false) {
        ToolResult result;
        result.is_error = is_error;
        result.content.emplace_back(ContentItem{
            .type = "text",
            .text = text
        });
        return result;
    }

    std::string UrlEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped << std::uppercase << std::hex << std::setfill('0');

        for (const unsigned char ch : value) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                escaped << ch;
            } else if (ch == ' ') {
                escaped << "%20";
            } else {
                escaped << '%' << std::setw(2) << static_cast<int>(ch);
            }
        }
        return escaped.str();
    }

    size_t WriteResponseBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* response_body = static_cast<std::string*>(userdata);
        response_body->append(ptr, size * nmemb);
        return size * nmemb;
    }

    json GetJsonFromPublicApi(const std::string& url) {
        static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curl_global_status != CURLE_OK) {
            throw std::runtime_error("curl global init failed");
        }

        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("curl init failed");
        }

        std::string response_body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        const CURLcode code = curl_easy_perform(curl);
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            throw std::runtime_error(std::string("request failed: ") + curl_easy_strerror(code));
        }
        if (http_status != 200) {
            throw std::runtime_error("request failed with HTTP status " + std::to_string(http_status));
        }
        return json::parse(response_body);
    }

    std::string WeatherCodeToText(int code) {
        switch (code) {
            case 0:
                return "晴";
            case 1:
                return "大部晴朗";
            case 2:
                return "局部多云";
            case 3:
                return "阴";
            case 45:
            case 48:
                return "雾";
            case 51:
            case 53:
            case 55:
                return "毛毛雨";
            case 56:
            case 57:
                return "冻毛毛雨";
            case 61:
            case 63:
            case 65:
                return "雨";
            case 66:
            case 67:
                return "冻雨";
            case 71:
            case 73:
            case 75:
                return "雪";
            case 77:
                return "雪粒";
            case 80:
            case 81:
            case 82:
                return "阵雨";
            case 85:
            case 86:
                return "阵雪";
            case 95:
                return "雷暴";
            case 96:
            case 99:
                return "雷暴伴冰雹";
            default:
                return "未知天气";
        }
    }

    std::string OptionalLocationField(const json& location, const std::string& key) {
        if (!location.contains(key) || location[key].is_null()) {
            return "";
        }
        return location[key].get<std::string>();
    }
}

//字符串转日志级别
spdlog::level::level_enum StringToLogLevel(const std::string& level) {
    if (level == "trace") {
        return spdlog::level::trace;
    } else if (level == "debug") {
        return spdlog::level::debug;
    } else if (level == "info") {
        return spdlog::level::info;
    } else if (level == "warn") {
        return spdlog::level::warn;
    } else if (level == "error") {
        return spdlog::level::err;
    } else if (level == "critical") {
        return spdlog::level::critical;
    }
    //default to in unknow
    return spdlog::level::info;
}
//注册MCP工具、资源、提示词
void SetMcpServer(McpServer& mcp) {
    //注册工具====================

    //echo工具
    {
        Tool tool;
        tool.name = "echo";
        tool.description = "Echo back the input message";
        tool.input_schema.properties = {
            {"message",{{"type", "string"}, {"description", "Message to echo"}}}
        };
        tool.input_schema.required = {"message"};

        mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
            ToolResult result;
            result.content.emplace_back(ContentItem{
                .type = "text",
                .text = "echo: " + args["message"].get<std::string>()
            });
            return result;
        });
    }
    //calculate
    {
        Tool tool;
        tool.name = "calculate";
        tool.description = "Perform basic arithmetic operations";
        tool.input_schema.properties = {
            {"operation",{{"type", "string"}, {"enum", json::array({"add", "subtract", "multiply", "divide", "modulo"})}}},
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        };
        tool.input_schema.required = {"operation", "a", "b"};

        mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
            std::string opera = args["operation"].get<std::string>();
            double a = args["a"].get<double>();
            double b = args["b"].get<double>();
            double value = 0;
            if (opera == "add") {
                value = a + b;
            } else if (opera == "subtract") {
                value = a - b;
            } else if (opera == "multiply") {
                value = a * b;
            } else if (opera == "divide") {
                if (b == 0) {
                    return MakeTextResult("divide by zero", true);
                }
                value = a / b;
            } else {
                return MakeTextResult("unsupported operation: " + opera, true);
            }
            return MakeTextResult(std::to_string(value));
        });
    }
    // GetTime工具
    {
        Tool tool;
        tool.name = "get_time";
        tool.description = "Get the current system time";
        tool.input_schema.properties = json::object();

        mcp.RegisterTool(tool, [](const json& /*args*/) -> ToolResult {
            std::time_t now = std::time(nullptr);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            ToolResult result;
            result.content.emplace_back(ContentItem{
                .type = "text",
                .text = buffer
            });
            return result;
        });
    }
    //GetWeather
    {
        Tool tool;
        tool.name = "get_weather";
        tool.description = "Get current weather for a city using the public Open-Meteo API";
        tool.input_schema.properties = {
            {"city", {{"type", "string"}, {"description", "City name (e.g., Beijing, Shanghai, 北京, 上海)"}}}
        };
        tool.input_schema.required = {"city"};

        mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
            try {
                if (!args.contains("city") || !args["city"].is_string()) {
                    return MakeTextResult("city 参数必须是字符串", true);
                }

                const auto city = args["city"].get<std::string>();
                if (city.empty()) {
                    return MakeTextResult("city 参数不能为空", true);
                }

                const auto geocoding = GetJsonFromPublicApi(
                    "https://geocoding-api.open-meteo.com/v1/search?name=" + UrlEncode(city) +
                    "&count=1&language=zh&format=json"
                );

                if (!geocoding.contains("results") || geocoding["results"].empty()) {
                    return MakeTextResult("未找到城市: " + city, true);
                }

                const auto& location = geocoding["results"].front();
                const double latitude = location.at("latitude").get<double>();
                const double longitude = location.at("longitude").get<double>();

                std::ostringstream weather_path;
                weather_path << "https://api.open-meteo.com/v1/forecast"
                             << "?latitude=" << latitude
                             << "&longitude=" << longitude
                             << "&current=temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m,wind_direction_10m"
                             << "&timezone=auto";

                const auto forecast = GetJsonFromPublicApi(weather_path.str());
                const auto& current = forecast.at("current");
                const auto& units = forecast.value("current_units", json::object());

                const auto location_name = OptionalLocationField(location, "name");
                const auto admin1 = OptionalLocationField(location, "admin1");
                const auto country = OptionalLocationField(location, "country");
                const int weather_code = current.value("weather_code", -1);

                std::ostringstream text;
                text << "城市: " << location_name;
                if (!admin1.empty()) {
                    text << ", " << admin1;
                }
                if (!country.empty()) {
                    text << ", " << country;
                }
                text << "\n时间: " << current.value("time", "")
                     << "\n天气: " << WeatherCodeToText(weather_code) << " (" << weather_code << ")"
                     << "\n温度: " << current.value("temperature_2m", 0.0) << " "
                     << units.value("temperature_2m", "°C")
                     << "\n湿度: " << current.value("relative_humidity_2m", 0) << " "
                     << units.value("relative_humidity_2m", "%")
                     << "\n降水: " << current.value("precipitation", 0.0) << " "
                     << units.value("precipitation", "mm")
                     << "\n风速: " << current.value("wind_speed_10m", 0.0) << " "
                     << units.value("wind_speed_10m", "km/h")
                     << "\n风向: " << current.value("wind_direction_10m", 0) << " "
                     << units.value("wind_direction_10m", "°")
                     << "\n数据来源: Open-Meteo public API";

                return MakeTextResult(text.str());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("获取天气失败: ") + e.what(), true);
            }
        });
    }
    //WriteFile工具
    {
        Tool tool;
        tool.name = "write_file";
        tool.description = "write content to a file";
        tool.input_schema.properties = {
            {"path", {{"type", "string"}, {"description", "File path to write to the file"}}},
            {"content", {{"type", "string"}, {"description", "File content to write to the file"}}}
        };
        tool.input_schema.required = {"path", "content"};

        mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
            ToolResult result;
            std::string path = args["path"].get<std::string>();
            std::string content = args["content"].get<std::string>();
            try {
                std::ofstream file(path);
                if (!file.is_open()) {
                    result.is_error = true;
                    result.content.emplace_back(ContentItem{
                        .type = "text",
                        .text = "Error: Failed to open file" + path
                    });
                    return result;
                }
                file << content;
                file.close();
                result.content.emplace_back(ContentItem{
                    .type = "text",
                    .text = "Successfully written to file" + path
                });
            } catch (const std::exception& e) {
                result.is_error = true;
                result.content.emplace_back(ContentItem{
                    .type = "text",
                    .text =  std::string("Error writing file: ") + e.what()
                });
            }
            return result;
        });
    }
    //==========注册资源resources


    //系统资源
    {
        Resource res;
        res.url = "system://info";
        res.name = "System Infomation";
        res.description = "Basic system information";
        res.mime_type = "text/plain";

        mcp.RegisterResource(res, [](const json& args)->ResourceContent {
            ResourceContent content;
            std::ostringstream oss;//输出到内存字符串（最后用 .str() 取出来）
            oss << "MCP-Server - System Info\n";
            oss << "========================\n";
            std::time_t now = time(nullptr);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            oss << "Time: " << buffer << "\n";
            content.text = oss.str();
            return content;
        });
    }
    //服务器配置
    {
        Resource res;
        res.url = "config://server";
        res.name = "Server Configuration";
        res.description = "Server configuration";
        res.mime_type = "application/json";

        mcp.RegisterResource(res, [](const std::string& url) -> ResourceContent {
            ResourceContent content;
            content.url = url;
            content.mime_type = "application/json";
            content.text = json {
                {"port", config::MCP_CONFIG.GetServerPort()},
                {"log_level", config::MCP_CONFIG.GetLogLevel()},
            }.dump(2);
            return content;
        });
    }

    //注册提示词Prompts
    //代码审查
    {
        Prompt prompt;
        prompt.name = "code_review";
        prompt.description = "Generate code review prompt";
        prompt.arguments.emplace_back(PromptArgument{
            .name = "code",
            .required = true,
        });
        prompt.arguments.emplace_back(PromptArgument{
            .name = "language",
            .required = true,
        });

        mcp.RegisterPrompt(prompt, [](const json& args) -> std::vector<PromptMessage> {
            std::vector<PromptMessage> pmsgs;
            PromptMessage pmsg;
            pmsg.role = Role::User;
            pmsg.content = {
                {"type", "text"},
                {"text", "Please review this " + args.at("language").get<std::string>() +
                         " code:\n\n" + args.at("code").get<std::string>()}
            };
            pmsgs.emplace_back(pmsg);
            return pmsgs;
        });
    }
    MCP_LOG_INFO("MCP setup complete: {} tools, {} resources, {} prompts",
                 mcp.ListTools().size(), mcp.ListResources().size(), mcp.ListPrompts().size());

}


//创建Dispatcher调度器
//initialize
jsonrpc::JsonRpcDispatcher CreateDispatcher(McpServer& mcp_server) {
    jsonrpc::JsonRpcDispatcher dispatcher;
    dispatcher.EnableThreadPool(config::MCP_CONFIG.GetThreadPoolSize(),
                                config::MCP_CONFIG.GetThreadPoolMaxQueueSize(),
                                config::MCP_CONFIG.GetThreadPoolPooledMethods());
    dispatcher.RegisterHandler("initialize", [&mcp_server](const json& /*param*/)->json {
        MCP_LOG_INFO("Initializing MCP server");
        return mcp_server.GetInitializationResult().to_json();
    });

    //tools/list
    dispatcher.RegisterHandler("tools/list", [&mcp_server](const json& /*params*/)->json {
        json tools_arr = json::array();
        for (const auto& tool : mcp_server.ListTools()) {
            tools_arr.emplace_back(tool.to_json());
        }
        return {{"tools", tools_arr}};
    });

    //tools/call
    dispatcher.RegisterHandler("tools/call", [&mcp_server](const json& params)->json {
        std::string name = params.value("name", "");
        json arguments = params.value("arguments", json::object());
        MCP_LOG_INFO("Calling tool: {}", name);
        auto result = mcp_server.GetTool(name, arguments);
        return result.to_json();
    });
    dispatcher.RegisterHandler("resources/list", [&mcp_server](const json& /*params*/)->json {
        json resources_arr = json::array();
        for (const auto& res : mcp_server.ListResources()) {
            resources_arr.emplace_back(res.to_json());
        }
        return {{"resources", resources_arr}};
    });
    dispatcher.RegisterHandler("resources/read", [&mcp_server](const json& params)->json {
        std::string url = params.value("url", "");
        MCP_LOG_INFO("Reading resources: {}", url);
        auto content = mcp_server.GetResource(url);
        json contents_arr = json::array();
        contents_arr.emplace_back(content.to_json());
        return {{"content", contents_arr}};
    });
    dispatcher.RegisterHandler("prompts/list", [&mcp_server](const json& /*params*/)->json {
        json prompts_arr = json::array();
        for (const auto& prompt : mcp_server.ListPrompts()) {
            prompts_arr.emplace_back(prompt.to_json());
        }
        return {{"prompts", prompts_arr}};
    });
    dispatcher.RegisterHandler("prompts/get", [&mcp_server](const json& params)->json {
        std::string name = params.value("name", "");
        json arguments = params.value("arguments", json::object());
        MCP_LOG_INFO("Reading prompts: {}", name);
        auto messages = mcp_server.GetPrompt(name, arguments);
        json messages_arr = json::array();
        for (const auto& message : messages) {
            messages_arr.emplace_back(message.to_json());
        }
        return {{"messages", messages_arr}};
    });
    return dispatcher;
}


//http模式
void run_http_mode(McpServer& mcp_server, const std::string& host, int port) {
    MCP_LOG_INFO("Running HTTP Server on {}:{}", host, port);
    auto dispatcher = CreateDispatcher(mcp_server);
    g_http_server = std::make_unique<jsonrpc::HttpJsonRpcServer>(std::move(dispatcher), host, port);
    //SSE事件队列
    struct {
        std::vector<json> events;
        std::mutex mutex;
        std::condition_variable cv;
    } event_queue;
    //MCP server的SSE回调
    mcp_server.SetSseCallback([&event_queue](const json& event) {
       std::lock_guard<std::mutex> lock(event_queue.mutex);
        event_queue.events.emplace_back(event);
        event_queue.cv.notify_all();
    });
    //注册SSE端点-服务器事件流
    g_http_server->RegisterSseEndpoint("/sse/events", [&mcp_server](const auto& send) {
        MCP_LOG_INFO("SSE events client connected");
        send(json({{"type", "connected"}, {"message", "Server events stream"}}).dump());

        int count = 0;
        while (g_running.load()) {
            json status = {
                {"type", "server_status"},
                {"timestamp", std::time(nullptr)},
                {"tools_count", mcp_server.ListTools().size()},
                {"resources_count", mcp_server.ListResources().size()},
                {"prompts_count", mcp_server.ListPrompts().size()},
                {"uptime_seconds", count++}
            };
            send(status.dump());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        MCP_LOG_INFO("SSE events client disconnected");
    });
    //注册SSE端点-工具调用实时流
    g_http_server->RegisterSseEndpoint("/sse/tool_calls", [&event_queue](const auto& send) {
        MCP_LOG_INFO("SSE tool_calls client connected");
        send(json({{"type", "connected"}, {"message", "Tool calls monitoring"}}).dump());
        while (g_running.load()) {
            std::unique_lock<std::mutex> lock(event_queue.mutex);

            event_queue.cv.wait_for(lock, std::chrono::seconds(1), [&event_queue]() {
                return !event_queue.events.empty();
            });
            for (const auto& event : event_queue.events) {
                send(event.dump());
            }
            event_queue.events.clear();
        }
        MCP_LOG_INFO("SSE tool calls client disconnected");
    });
    g_http_server->Run();
    MCP_LOG_INFO("HTTP Server terminated");
}
//stdio模式
void run_stdio_mode(McpServer& mcp_server) {
    MCP_LOG_INFO("MCP server started");
    auto dispatcher = CreateDispatcher(mcp_server);
    jsonrpc::StdioJsonRpcServer stdio_server(std::move(dispatcher));

    stdio_server.Run();

    MCP_LOG_INFO("MCP server stopped");
}
void run_both_modes(McpServer& mcp_server, const std::string& host, int port) {
    MCP_LOG_INFO("Starting both HTTP and stdio servers");

    std::thread http_thread([&mcp_server, &host, port]() {
       run_http_mode(mcp_server, host, port);
    });
    run_stdio_mode(mcp_server);

    if (http_thread.joinable()) {
        http_thread.join();
    }
}
int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file = "../config/server.json";
    std::string mode = "http";  // 默认 HTTP 模式
    std::string host;
    int port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --mode MODE      Server mode: http, stdio, or both (default: http)\n"
                      << "  --config FILE    Configuration file path\n"
                      << "  --host HOST      Server host (for HTTP mode)\n"
                      << "  --port PORT      Server port (for HTTP mode)\n"
                      << "  --help, -h       Show this help\n"
                      << "\nExamples:\n"
                      << "  " << argv[0] << " --mode http --port 8080\n"
                      << "  " << argv[0] << " --mode stdio\n"
                      << "  " << argv[0] << " --mode both --port 8080\n";
            return 0;
        }
    }
    // 验证模式
    if (mode != "http" && mode != "stdio" && mode != "both") {
        std::cerr << "Invalid mode: " << mode << " (must be http, stdio, or both)" << std::endl;
        return 1;
    }
    // 加载配置
    if (!config::MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
        return 1;
    }

    if (host.empty()) {
        host = "0.0.0.0";
    }
    if (port == 0) {
        port = config::MCP_CONFIG.GetServerPort();
    }
    // 初始化日志
    MCP_LOG_INIT("mcp_server", config::MCP_CONFIG.GetLogFilePath(),
                 config::MCP_CONFIG.GetLogFileSize(), config::MCP_CONFIG.GetLogFileCount(),
                 config::MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(config::MCP_CONFIG.GetLogLevel()));
    try {
        // 创建 MCP 服务器实例
        McpServer mcp_server("mcp-server", "1.0.0");

        // 设置能力
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false}; // 工具能力
        capabilities.resources = ServerCapabilities::ResourceCapability{false, false}; // 资源能力
        capabilities.prompts = ServerCapabilities::PromptCapability{false}; // 提示能力
        mcp_server.SetCapbilities(capabilities);

        // 注册 Tools, Resources, Prompts
        SetMcpServer(mcp_server);

        // 设置信号处理
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);

        // // 根据模式启动服务器
        if (mode == "http") {
            run_http_mode(mcp_server, host, port);
        } else if (mode == "stdio") {
            run_stdio_mode(mcp_server);
        } else if (mode == "both") {
            run_both_modes(mcp_server, host, port);
        }

        MCP_LOG_INFO("Server shutdown complete");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}












