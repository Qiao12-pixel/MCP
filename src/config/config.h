/**
 * @file config.h
 * @brief 解析配置文件
 * @author Joe
 * @date 26-4-28
 */


#ifndef CONFIG_H
#define CONFIG_H
#include <string>
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>

namespace mcp {
    namespace config {
        class Config {
            using json =  nlohmann::json;
        public:
            //配置单例模式
            static Config& GetInstance();

            bool LoadFromFile(std::string& path);
            bool IsLoad() const;

            int GetServerPort() const;

            std::string GetLogFilePath() const;
            std::string GetLogLevel() const;
            size_t GetLogFileSize() const;
            int GetLogFileCount() const;
            bool GetLogConsoleOutput() const;
            size_t GetThreadPoolSize() const;
            size_t GetThreadPoolMaxQueueSize() const;
            std::vector<std::string> GetThreadPoolPooledMethods() const;

            std::string GetImageGenerationDefaultProvider() const;
            std::string GetDoubaoImageApiKey() const;
            std::string GetDoubaoImageModel() const;
            std::string GetDoubaoImageApiUrl() const;
            std::string GetGeminiImageApiKey() const;
            std::string GetGeminiImageModel() const;
            /**
             * 可扩展获取其他字段方法，具体字段查看config/server.json
             */
        private:
            Config() = default;
            ~Config() = default;
            Config(const Config&) = delete;
            Config& operator=(const Config&) = delete;

            bool ValidateConfig() const;//校验server.json中的数据是否合法
            void SetDefaults();//为server.json中缺失的数据添加默认值
            std::string m_config_file_path_;
            bool m_loaded_ = false;
            json m_config_data_;//原始server.json配置数据
        };
#define MCP_CONFIG Config::GetInstance()
    }
}




#endif //CONFIG_H
