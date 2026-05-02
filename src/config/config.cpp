/**
 * @file config.cpp
 * @brief 
 * @author Joe
 * @date 26-4-28
 */


#include "config.h"
#include <iostream>
#include <fstream>

namespace mcp {
    namespace config {
        Config& Config::GetInstance() {
            static Config instance;
            return instance;
        }

        bool Config::LoadFromFile(std::string &config_file_path) {
            m_config_file_path_ = config_file_path;
            m_loaded_ = false;
            try {
                std::ifstream config_file(config_file_path);
                if (!config_file.is_open()) {
                    std::cerr << "Failed to open config file: " << config_file_path << std::endl;
                    return false;
                }
                config_file >> m_config_data_;
                config_file.close();
                //================校验部分
                SetDefaults();
                if (!ValidateConfig()) {
                    std::cerr << "Config validation failed" << std::endl;
                    return false;
                }
                m_loaded_ = true;
                return true;
            } catch (const json::parse_error& e) {
                std::cerr << "Config parse error: " << e.what() << std::endl;
                return false;
            } catch (const std::exception& e) {
                std::cerr << "Error loading config: " << e.what() << std::endl;
                return false;
            }
        }
        bool Config::IsLoad() const {
            return m_loaded_;
        }
        void Config::SetDefaults() {
            //set server default
            if (!m_config_data_.contains("server")) {
                m_config_data_["server"] = json::object();
            }

            auto& server = m_config_data_["server"];
            if (!server.contains("port")) {
                server["port"] = 8080;
            }

            //set logging default
            if (!m_config_data_.contains("logging")) {
                m_config_data_["logging"] = json::object();
            }
            auto& logging = m_config_data_["logging"];
            if (!logging.contains("log_file_path")) {
                logging["log_file_path"] = "../logs/server.log";
            } if (!logging.contains("log_level")) {
                logging["log_level"] = "info";
            } if (!logging.contains("log_file_size")) {
                logging["log_file_size"] = 10 * 1024 * 1024;
            } if (!logging.contains("log_file_count")) {
                logging["log_file_count"] = 5;
            } if (!logging.contains("log_console_output")) {
                logging["log_console_output"] = true;
            }
        }
        bool Config::ValidateConfig() const {
            //Check server require
            if (!m_config_data_.contains("server")) {
                std::cerr << "Config does not contain server" << std::endl;
                return false;
            }

            //Check port require
            int port = m_config_data_["server"].value("port", 8080);
            //valid valueble
            if (port < 1 || port > 65535) {
                std::cerr << "Invalid port number" << std::endl;
                return false;
            }

            //check logging
            if (m_config_data_.contains("logging")) {
                std::string log_level = m_config_data_["logging"].value("log_level", "info");
                if (log_level != "trace" && log_level != "debug" && log_level != "info" && log_level != "warn" && log_level != "error" && log_level != "critical") {
                    std::cerr << "Invalid logging level" << std::endl;
                    std::cerr << "Valid levels: trace, debug, info, warn, error, critical" << std::endl;
                    return false;
                }
                size_t log_file_size = m_config_data_["logging"].value("log_file_size", 10 * 1024 * 1024);
                if (log_file_size == 0) {
                    std::cerr << "log_file_size must be greater than 0" << std::endl;
                    return false;
                }
                int log_file_count = m_config_data_["logging"].value("log_file_count", 5);
                if (log_file_count <= 0) {
                    std::cerr << "log_file_count must be greater than 0" << std::endl;
                    return false;
                }
            }
            // ===================================================================
            // 🔧 Extension point: Add validation logic for new fields here
            // ===================================================================
            // Example:
            // if (!config_data_.contains("database")) {
            //     std::cerr << "Missing 'database' section in config" << std::endl;
            //     return false;
            // }
            //
            // std::string db_url = config_data_["database"].value("url", "");
            // if (db_url.empty()) {
            //     std::cerr << "Database URL cannot be empty" << std::endl;
            //     return false;
            // }
            // ===================================================================
            return true;
        }
        //Server
        int Config::GetServerPort() const {
            return m_config_data_["server"].value("port", 8080);
        }
        //logging
        std::string Config::GetLogFilePath() const {
            return m_config_data_["logging"].value("log_file_path", std::string("../logs/server.log"));
        }
        std::string Config::GetLogLevel() const {
            return m_config_data_["logging"].value("log_level", std::string("info"));
        }
        size_t Config::GetLogFileSize() const {
            return m_config_data_["logging"].value("log_file_size", 10 * 1024 * 1024); // Default 10MB
        }

        int Config::GetLogFileCount() const {
            return m_config_data_["logging"].value("log_file_count", 5);
        }

        bool Config::GetLogConsoleOutput() const {
            return m_config_data_["logging"].value("log_console_output", true);
        }

        // ===================================================================
        // 🔧 Extension point: Implement new config getter methods here
        // ===================================================================
        // Example:
        // std::string Config::GetServerHost() const {
        //     return config_data_["server"].value("host", std::string("0.0.0.0"));
        // }
        //
        // std::string Config::GetLogFilePath() const {
        //     return config_data_["logging"].value("log_file_path", std::string("logs/server.log"));
        // }
        //
        // std::string Config::GetDatabaseUrl() const {
        //     return config_data_["database"].value("url", std::string(""));
        // }
        // ===================================================================



    }
}
