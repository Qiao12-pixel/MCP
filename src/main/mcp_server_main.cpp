#include <iostream>
#include <string>

#include "config/config.h"
#include "logger/logger.h"
#include "mcp/mcp_server.h"
#include "server_app.h"

using namespace mcp;

int main(int argc, char* argv[]) {
    std::string config_file = "../config/server.json";
    std::string mode = "http";
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

    if (mode != "http" && mode != "stdio" && mode != "both") {
        std::cerr << "Invalid mode: " << mode << " (must be http, stdio, or both)" << std::endl;
        return 1;
    }

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

    MCP_LOG_INIT("mcp_server", config::MCP_CONFIG.GetLogFilePath(),
                 config::MCP_CONFIG.GetLogFileSize(), config::MCP_CONFIG.GetLogFileCount(),
                 config::MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(config::MCP_CONFIG.GetLogLevel()));

    try {
        McpServer mcp_server("mcp-server", "1.0.0");
        ConfigureMcpServer(mcp_server);
        InstallSignalHandlers();

        if (mode == "http") {
            RunHttpMode(mcp_server, host, port);
        } else if (mode == "stdio") {
            RunStdioMode(mcp_server);
        } else if (mode == "both") {
            RunBothModes(mcp_server, host, port);
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
