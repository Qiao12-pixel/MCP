#include "mcp_builtin_tools.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <curl/curl.h>

#include "config/config.h"

namespace mcp {
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

        struct ImageSizeConfig {
            std::string aspect_ratio;
            std::string image_size;
        };

        ImageSizeConfig ResolveImageSizeConfig(const std::string& size) {
            if (size == "2K") {
                return {.aspect_ratio = "1:1", .image_size = "2K"};
            }
            if (size == "4K") {
                return {.aspect_ratio = "1:1", .image_size = "4K"};
            }
            if (size == "512x512" || size == "1024x1024") {
                return {.aspect_ratio = "1:1", .image_size = "1K"};
            }
            if (size == "1024x1792") {
                return {.aspect_ratio = "9:16", .image_size = "1K"};
            }
            if (size == "1792x1024") {
                return {.aspect_ratio = "16:9", .image_size = "1K"};
            }
            throw std::runtime_error("unsupported image size: " + size);
        }

        std::string GetImageFileExtension(const std::string& mime_type) {
            if (mime_type == "image/jpeg" || mime_type == "image/jpg") {
                return ".jpg";
            }
            if (mime_type == "image/webp") {
                return ".webp";
            }
            return ".png";
        }

        bool HasImageFileExtension(const std::string& filename) {
            const auto dot = filename.find_last_of('.');
            if (dot == std::string::npos) {
                return false;
            }
            auto ext = filename.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
        }

        std::string ValidateAndNormalizeImageFilename(const std::string& filename,
                                                      const std::string& mime_type) {
            if (filename.empty()) {
                throw std::runtime_error("filename cannot be empty");
            }
            if (filename == "." || filename == ".." ||
                filename.find('/') != std::string::npos ||
                filename.find('\\') != std::string::npos ||
                filename.find("..") != std::string::npos) {
                throw std::runtime_error("filename must be a simple file name under generated/images");
            }
            for (const unsigned char ch : filename) {
                if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.') {
                    throw std::runtime_error("filename may only contain letters, numbers, '.', '_' and '-'");
                }
            }
            if (HasImageFileExtension(filename)) {
                return filename;
            }
            return filename + GetImageFileExtension(mime_type);
        }

        std::vector<unsigned char> DecodeBase64(const std::string& input) {
            static const std::string chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<int> decode_table(256, -1);
            for (size_t i = 0; i < chars.size(); ++i) {
                decode_table[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
            }

            std::vector<unsigned char> output;
            int val = 0;
            int valb = -8;
            for (const unsigned char ch : input) {
                if (std::isspace(ch)) {
                    continue;
                }
                if (ch == '=') {
                    break;
                }
                const int decoded = decode_table[ch];
                if (decoded == -1) {
                    throw std::runtime_error("invalid base64 image data");
                }
                val = (val << 6) + decoded;
                valb += 6;
                if (valb >= 0) {
                    output.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
                    valb -= 8;
                }
            }
            return output;
        }

        json PostJsonToGemini(const std::string& model,
                              const std::string& api_key,
                              const json& request_body_json) {
            static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (curl_global_status != CURLE_OK) {
                throw std::runtime_error("curl global init failed");
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init failed");
            }

            const std::string url =
                "https://generativelanguage.googleapis.com/v1beta/models/" +
                model + ":generateContent";
            const std::string request_body = request_body_json.dump();
            std::string response_body;

            struct curl_slist* headers = nullptr;
            const std::string api_key_header = "x-goog-api-key: " + api_key;
            headers = curl_slist_append(headers, api_key_header.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            const CURLcode code = curl_easy_perform(curl);
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK) {
                throw std::runtime_error(std::string("Gemini request failed: ") + curl_easy_strerror(code));
            }
            if (http_status < 200 || http_status >= 300) {
                throw std::runtime_error("Gemini request failed with HTTP status " +
                                         std::to_string(http_status) + ": " + response_body);
            }

            return json::parse(response_body);
        }

        json PostJsonToDoubao(const std::string& api_url,
                              const std::string& api_key,
                              const json& request_body_json) {
            static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (curl_global_status != CURLE_OK) {
                throw std::runtime_error("curl global init failed");
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init failed");
            }

            const std::string request_body = request_body_json.dump();
            std::string response_body;

            struct curl_slist* headers = nullptr;
            const std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            const CURLcode code = curl_easy_perform(curl);
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK) {
                throw std::runtime_error(std::string("Doubao request failed: ") + curl_easy_strerror(code));
            }
            if (http_status < 200 || http_status >= 300) {
                throw std::runtime_error("Doubao request failed with HTTP status " +
                                         std::to_string(http_status) + ": " + response_body);
            }

            return json::parse(response_body);
        }

        std::pair<std::string, std::string> ExtractGeminiInlineImage(const json& response) {
            if (!response.contains("candidates") || !response["candidates"].is_array()) {
                throw std::runtime_error("Gemini response does not contain candidates");
            }
            for (const auto& candidate : response["candidates"]) {
                if (!candidate.contains("content")) {
                    continue;
                }
                const auto& content = candidate["content"];
                if (!content.contains("parts") || !content["parts"].is_array()) {
                    continue;
                }
                for (const auto& part : content["parts"]) {
                    const json* inline_data = nullptr;
                    if (part.contains("inlineData")) {
                        inline_data = &part["inlineData"];
                    } else if (part.contains("inline_data")) {
                        inline_data = &part["inline_data"];
                    }
                    if (inline_data == nullptr || !inline_data->contains("data")) {
                        continue;
                    }

                    std::string mime_type = "image/png";
                    if (inline_data->contains("mimeType")) {
                        mime_type = inline_data->value("mimeType", std::string("image/png"));
                    } else if (inline_data->contains("mime_type")) {
                        mime_type = inline_data->value("mime_type", std::string("image/png"));
                    }
                    return {inline_data->at("data").get<std::string>(), mime_type};
                }
            }
            throw std::runtime_error("Gemini response did not include inline image data");
        }

        std::pair<std::string, std::string> ExtractDoubaoBase64Image(const json& response) {
            if (!response.contains("data") || !response["data"].is_array() || response["data"].empty()) {
                throw std::runtime_error("Doubao response does not contain data array");
            }

            const auto& first_image = response["data"].front();
            if (first_image.contains("b64_json") && first_image["b64_json"].is_string()) {
                return {first_image["b64_json"].get<std::string>(), "image/png"};
            }
            if (first_image.contains("b64Json") && first_image["b64Json"].is_string()) {
                return {first_image["b64Json"].get<std::string>(), "image/png"};
            }
            if (first_image.contains("url")) {
                throw std::runtime_error("Doubao returned an image URL, but b64_json was expected");
            }
            throw std::runtime_error("Doubao response did not include b64_json image data");
        }

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        std::filesystem::path SaveGeneratedImage(const std::string& filename,
                                                 const std::string& mime_type,
                                                 const std::vector<unsigned char>& image_bytes) {
            const std::filesystem::path output_dir = std::filesystem::path("generated") / "images";
            std::filesystem::create_directories(output_dir);

            const auto normalized_filename = ValidateAndNormalizeImageFilename(filename, mime_type);
            const std::filesystem::path output_path = output_dir / normalized_filename;

            std::ofstream file(output_path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("failed to open image file for writing: " + output_path.string());
            }
            file.write(reinterpret_cast<const char*>(image_bytes.data()),
                       static_cast<std::streamsize>(image_bytes.size()));
            if (!file.good()) {
                throw std::runtime_error("failed to write image file: " + output_path.string());
            }
            return output_path;
        }

    }

    void RegisterBuiltinTools(McpServer& mcp) {
        {
            Tool tool;
            tool.name = "echo";
            tool.description = "Echo back the input message";
            tool.input_schema.properties = {
                {"message", {{"type", "string"}, {"description", "Message to echo"}}}
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

        {
            Tool tool;
            tool.name = "calculate";
            tool.description = "Perform basic arithmetic operations";
            tool.input_schema.properties = {
                {"operation", {{"type", "string"}, {"enum", json::array({"add", "subtract", "multiply", "divide", "modulo"})}}},
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

        {
            Tool tool;
            tool.name = "generate_image";
            tool.description = "Generate an image from a prompt and save it to a local file";
            tool.input_schema.properties = {
                {"prompt", {{"type", "string"}, {"description", "Image prompt provided by the client model"}}},
                {"filename", {{"type", "string"},{"description", "Output file name, saved under generated/images"}}},
                {"size", {{"type", "string"}, {"description", "Image size e.g. 1024x1024 or 2K"}, {"enum", json::array({"512x512", "1024x1024", "1024x1792", "1792x1024", "2K", "4K"})}}},
                {"provider", {{"type", "string"}, {"description", "Image provider, defaults to server configuration"}, {"enum", json::array({"doubao", "gemini"})}}},
            };
            tool.input_schema.required = {"prompt", "filename"};
            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("prompt") || !args["prompt"].is_string()) {
                        return MakeTextResult("Error: Missing required argument prompt", true);
                    }
                    if (!args.contains("filename") || !args["filename"].is_string()) {
                        return MakeTextResult("Error: Missing required argument filename", true);
                    }
                    const auto prompt = args["prompt"].get<std::string>();
                    const auto filename = args["filename"].get<std::string>();
                    const auto size = args.value("size", std::string("1024x1024"));
                    if (prompt.empty()) {
                        return MakeTextResult("Error: Missing required argument prompt", true);
                    }
                    if (filename.empty()) {
                        return MakeTextResult("Error: Missing required argument filename", true);
                    }

                    const auto provider = ToLower(args.value(
                        "provider",
                        config::MCP_CONFIG.GetImageGenerationDefaultProvider()
                    ));

                    std::string image_base64;
                    std::string mime_type = "image/png";
                    std::string model;

                    if (provider == "doubao") {
                        const std::string api_key = config::MCP_CONFIG.GetDoubaoImageApiKey();
                        if (api_key.empty()) {
                            return MakeTextResult(
                                "Doubao API key is not configured: set image_generation.doubao.api_key in server.json",
                                true
                            );
                        }

                        model = config::MCP_CONFIG.GetDoubaoImageModel();
                        const std::string api_url = config::MCP_CONFIG.GetDoubaoImageApiUrl();

                        const json request_body = {
                            {"model", model},
                            {"prompt", prompt},
                            {"size", size},
                            {"response_format", "b64_json"},
                            {"stream", false},
                            {"watermark", false}
                        };

                        const auto response = PostJsonToDoubao(api_url, api_key, request_body);
                        std::tie(image_base64, mime_type) = ExtractDoubaoBase64Image(response);
                    } else if (provider == "gemini") {
                        const std::string api_key = config::MCP_CONFIG.GetGeminiImageApiKey();
                        if (api_key.empty()) {
                            return MakeTextResult(
                                "Gemini API key is not configured: set image_generation.gemini.api_key in server.json",
                                true
                            );
                        }

                        model = config::MCP_CONFIG.GetGeminiImageModel();
                        const auto size_config = ResolveImageSizeConfig(size);

                        const json request_body = {
                            {"contents", json::array({
                                {
                                    {"parts", json::array({
                                        {{"text", prompt}}
                                    })}
                                }
                            })},
                            {"generationConfig", {
                                {"responseModalities", json::array({"IMAGE"})},
                                {"imageConfig", {
                                    {"aspectRatio", size_config.aspect_ratio},
                                    {"imageSize", size_config.image_size}
                                }}
                            }}
                        };

                        const auto response = PostJsonToGemini(model, api_key, request_body);
                        std::tie(image_base64, mime_type) = ExtractGeminiInlineImage(response);
                    } else {
                        return MakeTextResult("unsupported image provider: " + provider, true);
                    }

                    const auto image_bytes = DecodeBase64(image_base64);
                    if (image_bytes.empty()) {
                        return MakeTextResult(provider + " returned empty image data", true);
                    }
                    const auto output_path = SaveGeneratedImage(filename, mime_type, image_bytes);

                    return MakeTextResult("Image saved to: " + output_path.string() +
                                          "\nprovider: " + provider +
                                          "\nmodel: " + model);
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("Generate image failed: ") + e.what(), true);
                }
            });
        }

    }
}
