/**
 * @file config.cpp
 * @brief 
 * @author Joe
 * @date 26-4-28
 */


#include "config.h"
#include <iostream>
#include <fstream>
#include <unordered_set>

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

            //set thread pool default
            if (!m_config_data_.contains("thread_pool")) {
                m_config_data_["thread_pool"] = json::object();
            }
            auto& thread_pool = m_config_data_["thread_pool"];
            if (!thread_pool.contains("size")) {
                thread_pool["size"] = 4;
            }
            if (!thread_pool.contains("max_queue_size")) {
                thread_pool["max_queue_size"] = 128;
            }
            if (!thread_pool.contains("pooled_methods")) {
                thread_pool["pooled_methods"] = json::array({
                    "tools/call",
                    "resources/read",
                    "prompts/get"
                });
            }

            //set image generation default
            if (!m_config_data_.contains("image_generation")) {
                m_config_data_["image_generation"] = json::object();
            }
            auto& image_generation = m_config_data_["image_generation"];
            if (!image_generation.is_object()) {
                return;
            }
            if (!image_generation.contains("default_provider")) {
                image_generation["default_provider"] = "doubao";
            }

            if (!image_generation.contains("doubao")) {
                image_generation["doubao"] = json::object();
            }
            auto& doubao = image_generation["doubao"];
            if (!doubao.is_object()) {
                return;
            }
            if (!doubao.contains("api_key")) {
                doubao["api_key"] = "";
            }
            if (!doubao.contains("model")) {
                doubao["model"] = "doubao-seedream-4-5-251128";
            }
            if (!doubao.contains("api_url")) {
                doubao["api_url"] = "https://ark.cn-beijing.volces.com/api/v3/images/generations";
            }

            if (!image_generation.contains("gemini")) {
                image_generation["gemini"] = json::object();
            }
            auto& gemini = image_generation["gemini"];
            if (!gemini.is_object()) {
                return;
            }
            if (!gemini.contains("api_key")) {
                gemini["api_key"] = "";
            }
            if (!gemini.contains("model")) {
                gemini["model"] = "gemini-3.1-flash-image-preview";
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

            if (m_config_data_.contains("thread_pool")) {
                size_t thread_pool_size = m_config_data_["thread_pool"].value("size", 4);
                if (thread_pool_size == 0) {
                    std::cerr << "thread_pool.size must be greater than 0" << std::endl;
                    return false;
                }
                size_t max_queue_size = m_config_data_["thread_pool"].value("max_queue_size", 128);
                if (max_queue_size == 0) {
                    std::cerr << "thread_pool.max_queue_size must be greater than 0" << std::endl;
                    return false;
                }
                if (!m_config_data_["thread_pool"].contains("pooled_methods") ||
                    !m_config_data_["thread_pool"]["pooled_methods"].is_array()) {
                    std::cerr << "thread_pool.pooled_methods must be an array" << std::endl;
                    return false;
                }

                std::unordered_set<std::string> seen_methods;
                for (const auto& method : m_config_data_["thread_pool"]["pooled_methods"]) {
                    if (!method.is_string()) {
                        std::cerr << "thread_pool.pooled_methods must contain strings only" << std::endl;
                        return false;
                    }
                    const auto method_name = method.get<std::string>();
                    if (method_name.empty()) {
                        std::cerr << "thread_pool.pooled_methods cannot contain empty method names" << std::endl;
                        return false;
                    }
                    if (!seen_methods.insert(method_name).second) {
                        std::cerr << "thread_pool.pooled_methods contains duplicate method: " << method_name << std::endl;
                        return false;
                    }
                }
            }

            if (m_config_data_.contains("image_generation")) {
                const auto& image_generation = m_config_data_["image_generation"];
                if (!image_generation.is_object()) {
                    std::cerr << "image_generation must be an object" << std::endl;
                    return false;
                }

                if (!image_generation["default_provider"].is_string()) {
                    std::cerr << "image_generation.default_provider must be a string" << std::endl;
                    return false;
                }
                const std::string default_provider = image_generation["default_provider"].get<std::string>();
                if (default_provider != "doubao" && default_provider != "gemini") {
                    std::cerr << "image_generation.default_provider must be doubao or gemini" << std::endl;
                    return false;
                }

                if (!image_generation.contains("doubao") || !image_generation["doubao"].is_object()) {
                    std::cerr << "image_generation.doubao must be an object" << std::endl;
                    return false;
                }
                const auto& doubao = image_generation["doubao"];
                if (!doubao["api_key"].is_string()) {
                    std::cerr << "image_generation.doubao.api_key must be a string" << std::endl;
                    return false;
                }
                if (!doubao["model"].is_string() || doubao["model"].get<std::string>().empty()) {
                    std::cerr << "image_generation.doubao.model cannot be empty" << std::endl;
                    return false;
                }
                if (!doubao["api_url"].is_string() || doubao["api_url"].get<std::string>().empty()) {
                    std::cerr << "image_generation.doubao.api_url cannot be empty" << std::endl;
                    return false;
                }

                if (!image_generation.contains("gemini") || !image_generation["gemini"].is_object()) {
                    std::cerr << "image_generation.gemini must be an object" << std::endl;
                    return false;
                }
                const auto& gemini = image_generation["gemini"];
                if (!gemini["api_key"].is_string()) {
                    std::cerr << "image_generation.gemini.api_key must be a string" << std::endl;
                    return false;
                }
                if (!gemini["model"].is_string() || gemini["model"].get<std::string>().empty()) {
                    std::cerr << "image_generation.gemini.model cannot be empty" << std::endl;
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

        size_t Config::GetThreadPoolSize() const {
            return m_config_data_["thread_pool"].value("size", 4);
        }

        size_t Config::GetThreadPoolMaxQueueSize() const {
            return m_config_data_["thread_pool"].value("max_queue_size", 128);
        }

        std::vector<std::string> Config::GetThreadPoolPooledMethods() const {
            return m_config_data_["thread_pool"].value("pooled_methods", std::vector<std::string>{
                "tools/call",
                "resources/read",
                "prompts/get"
            });
        }

        std::string Config::GetImageGenerationDefaultProvider() const {
            return m_config_data_["image_generation"].value("default_provider", std::string("doubao"));
        }

        std::string Config::GetDoubaoImageApiKey() const {
            return m_config_data_["image_generation"]["doubao"].value("api_key", std::string());
        }

        std::string Config::GetDoubaoImageModel() const {
            return m_config_data_["image_generation"]["doubao"].value(
                "model",
                std::string("doubao-seedream-4-5-251128")
            );
        }

        std::string Config::GetDoubaoImageApiUrl() const {
            return m_config_data_["image_generation"]["doubao"].value(
                "api_url",
                std::string("https://ark.cn-beijing.volces.com/api/v3/images/generations")
            );
        }

        std::string Config::GetGeminiImageApiKey() const {
            return m_config_data_["image_generation"]["gemini"].value("api_key", std::string());
        }

        std::string Config::GetGeminiImageModel() const {
            return m_config_data_["image_generation"]["gemini"].value(
                "model",
                std::string("gemini-3.1-flash-image-preview")
            );
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
