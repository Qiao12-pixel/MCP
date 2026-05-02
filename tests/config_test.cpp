#include <config/config.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path WriteTempConfig(const std::string& content) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("mcp_config_test_" + std::to_string(now) + ".json");

    std::ofstream file(path);
    file << content;
    file.close();

    return path;
}

bool LoadConfigFromPath(const std::filesystem::path& path) {
    auto config_path = path.string();
    return mcp::config::MCP_CONFIG.LoadFromFile(config_path);
}

}  // namespace

TEST(ConfigTest, LoadsCompleteConfigAndReturnsConfiguredValues) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 9001
        },
        "logging": {
            "log_file_path": "/tmp/mcp-test.log",
            "log_level": "debug",
            "log_file_size": 4096,
            "log_file_count": 7,
            "log_console_output": false
        }
    })");

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_TRUE(config.IsLoad());
    EXPECT_EQ(config.GetServerPort(), 9001);
    EXPECT_EQ(config.GetLogFilePath(), "/tmp/mcp-test.log");
    EXPECT_EQ(config.GetLogLevel(), "debug");
    EXPECT_EQ(config.GetLogFileSize(), 4096);
    EXPECT_EQ(config.GetLogFileCount(), 7);
    EXPECT_FALSE(config.GetLogConsoleOutput());
}

TEST(ConfigTest, LoadsServerJsonFromProjectConfigDirectory) {
    const auto path = std::filesystem::path(MCP_PROJECT_ROOT) / "config" / "server.json";

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_TRUE(config.IsLoad());
    EXPECT_EQ(config.GetServerPort(), 8080);
    EXPECT_EQ(config.GetLogFilePath(), "../logs/server.log");
    EXPECT_EQ(config.GetLogLevel(), "debug");
    EXPECT_EQ(config.GetLogFileSize(), 52428800);
    EXPECT_EQ(config.GetLogFileCount(), 5);
    EXPECT_TRUE(config.GetLogConsoleOutput());
}

TEST(ConfigTest, UsesDefaultsWhenSectionsOrFieldsAreMissing) {
    const auto path = WriteTempConfig("{}");

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_EQ(config.GetServerPort(), 8080);
    EXPECT_EQ(config.GetLogFilePath(), "../logs/server.log");
    EXPECT_EQ(config.GetLogLevel(), "info");
    EXPECT_EQ(config.GetLogFileSize(), 10 * 1024 * 1024);
    EXPECT_EQ(config.GetLogFileCount(), 5);
    EXPECT_TRUE(config.GetLogConsoleOutput());
}

TEST(ConfigTest, ReturnsFalseForMissingFile) {
    const auto valid_path = WriteTempConfig(R"({
        "server": {
            "port": 9002
        }
    })");
    ASSERT_TRUE(LoadConfigFromPath(valid_path));
    ASSERT_TRUE(mcp::config::MCP_CONFIG.IsLoad());

    auto missing_path = std::filesystem::temp_directory_path() / "mcp_config_test_missing.json";
    auto missing_path_string = missing_path.string();

    EXPECT_FALSE(mcp::config::MCP_CONFIG.LoadFromFile(missing_path_string));
    EXPECT_FALSE(mcp::config::MCP_CONFIG.IsLoad());
}

TEST(ConfigTest, ReturnsFalseForInvalidJson) {
    const auto path = WriteTempConfig("{ invalid json");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsPortBelowValidRange) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsPortAboveValidRange) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 65536
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsInvalidLogLevel) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_level": "verbose"
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsZeroLogFileSize) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_file_size": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsNonPositiveLogFileCount) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_file_count": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}
