#include <json_rpc/jsonrpc_dispatcher.h>
#include <json_rpc/jsonrpc_serialization.h>
#include <json_rpc/stdio_jsonrpc_server.h>
#include <logger/logger.h>
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>

namespace {

using mcp::jsonrpc::JsonRpcDispatcher;
using mcp::jsonrpc::JsonRpcError;
using mcp::jsonrpc::JsonRpcRequest;
using mcp::jsonrpc::JsonRpcResponse;
using mcp::jsonrpc::StdioJsonRpcServer;
using mcp::jsonrpc::json;

std::string Frame(const json& message) {
    const std::string payload = message.dump();
    return "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
}

json ReadFramedMessage(const std::string& output) {
    const std::string prefix = "Content-Length: ";
    EXPECT_EQ(output.rfind(prefix, 0), 0);

    const auto header_end = output.find("\r\n\r\n");
    EXPECT_NE(header_end, std::string::npos);

    const auto length_begin = prefix.size();
    const auto length_end = output.find("\r\n", length_begin);
    const auto content_length = static_cast<size_t>(
            std::stoul(output.substr(length_begin, length_end - length_begin)));

    const auto payload_begin = header_end + 4;
    const auto payload = output.substr(payload_begin);
    EXPECT_EQ(payload.size(), content_length);

    return json::parse(payload);
}

class JsonRpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
        mcp::logger::Logger::GetInstance().Init("jsonrpc_test", "", 1024 * 1024, 3, false);
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }
};

}  // namespace

TEST(JsonRpcSerializationTest, DeserializesRequestWithParamsAndId) {
    const json input = {
            {"jsonrpc", "2.0"},
            {"method", "tools/list"},
            {"params", {{"cursor", "abc"}}},
            {"id", 12}
    };

    const auto req = input.get<JsonRpcRequest>();

    EXPECT_EQ(req.jsonrpc, "2.0");
    EXPECT_EQ(req.method, "tools/list");
    ASSERT_TRUE(req.params.has_value());
    EXPECT_EQ(req.params->at("cursor"), "abc");
    ASSERT_TRUE(req.id.has_value());
    EXPECT_EQ(req.id.value(), 12);
}

TEST(JsonRpcSerializationTest, DeserializesNotificationWithoutId) {
    const json input = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"}
    };

    const auto req = input.get<JsonRpcRequest>();

    EXPECT_EQ(req.method, "notifications/initialized");
    EXPECT_FALSE(req.id.has_value());
    EXPECT_FALSE(req.params.has_value());
}

TEST(JsonRpcSerializationTest, SerializesResultResponse) {
    JsonRpcResponse response;
    response.id = "req-1";
    response.result = json{{"ok", true}};

    const json output = response;

    EXPECT_EQ(output.at("jsonrpc"), "2.0");
    EXPECT_EQ(output.at("id"), "req-1");
    EXPECT_EQ(output.at("result").at("ok"), true);
    EXPECT_FALSE(output.contains("error"));
}

TEST(JsonRpcSerializationTest, SerializesErrorResponse) {
    JsonRpcResponse response;
    response.id = nullptr;
    response.error = JsonRpcError{-32601, "Method not found", std::nullopt};

    const json output = response;

    EXPECT_EQ(output.at("error").at("code"), -32601);
    EXPECT_EQ(output.at("error").at("message"), "Method not found");
    EXPECT_FALSE(output.at("error").contains("data"));
}

TEST(JsonRpcSerializationTest, ResponseFromJsonClearsStaleAlternativesWhenReused) {
    JsonRpcResponse response;
    response.error = JsonRpcError{-32603, "old error", std::nullopt};

    mcp::jsonrpc::from_json(json{
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {{"ok", true}}}
    }, response);

    ASSERT_TRUE(response.result.has_value());
    EXPECT_EQ(response.result->at("ok"), true);
    EXPECT_FALSE(response.error.has_value());

    mcp::jsonrpc::from_json(json{
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"error", {{"code", -32601}, {"message", "missing"}}}
    }, response);

    ASSERT_TRUE(response.error.has_value());
    EXPECT_EQ(response.error->code, -32601);
    EXPECT_FALSE(response.result.has_value());
}

TEST(JsonRpcDispatcherTest, DispatchesRegisteredHandler) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("add", [](const json& params) {
        return params.at("a").get<int>() + params.at("b").get<int>();
    });

    EXPECT_TRUE(dispatcher.HasHandler("add"));
    EXPECT_EQ(dispatcher.Call("add", json{{"a", 2}, {"b", 3}}), 5);
}

TEST(JsonRpcDispatcherTest, ThrowsForUnknownMethod) {
    JsonRpcDispatcher dispatcher;

    EXPECT_FALSE(dispatcher.HasHandler("missing"));
    EXPECT_THROW(dispatcher.Call("missing", json::object()), std::runtime_error);
}

TEST_F(JsonRpcServerTest, RunsRequestAndWritesFramedResultResponse) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("echo", [](const json& params) {
        return json{{"echo", params.at("message")}};
    });

    const json request = {
            {"jsonrpc", "2.0"},
            {"method", "echo"},
            {"params", {{"message", "hello"}}},
            {"id", 1}
    };
    std::istringstream input(Frame(request));
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_EQ(response.at("id"), 1);
    EXPECT_EQ(response.at("result").at("echo"), "hello");
    EXPECT_FALSE(response.contains("error"));
}

TEST_F(JsonRpcServerTest, AcceptsNormalContentLengthHeaderCasing) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("ping", [](const json&) {
        return json{{"pong", true}};
    });

    const json request = {
            {"jsonrpc", "2.0"},
            {"method", "ping"},
            {"id", "abc"}
    };
    const std::string payload = request.dump();
    std::istringstream input("Content-Length: " + std::to_string(payload.size()) +
                             "\r\n\r\n" + payload);
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_EQ(response.at("id"), "abc");
    EXPECT_EQ(response.at("result").at("pong"), true);
}

TEST_F(JsonRpcServerTest, DoesNotRespondToNotification) {
    JsonRpcDispatcher dispatcher;
    bool called = false;
    dispatcher.RegisterHandler("notify", [&called](const json&) {
        called = true;
        return json{{"ignored", true}};
    });

    const json notification = {
            {"jsonrpc", "2.0"},
            {"method", "notify"}
    };
    std::istringstream input(Frame(notification));
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    EXPECT_TRUE(called);
    EXPECT_TRUE(output.str().empty());
}

TEST_F(JsonRpcServerTest, ReturnsMethodNotFoundError) {
    JsonRpcDispatcher dispatcher;
    const json request = {
            {"jsonrpc", "2.0"},
            {"method", "missing"},
            {"id", 99}
    };
    std::istringstream input(Frame(request));
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_EQ(response.at("id"), 99);
    EXPECT_EQ(response.at("error").at("code"), -32601);
}

TEST_F(JsonRpcServerTest, PassesEmptyObjectWhenParamsAreMissing) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("inspect_params", [](const json& params) {
        return json{{"is_object", params.is_object()}, {"empty", params.empty()}};
    });

    const json request = {
            {"jsonrpc", "2.0"},
            {"method", "inspect_params"},
            {"id", 7}
    };
    std::istringstream input(Frame(request));
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_EQ(response.at("result").at("is_object"), true);
    EXPECT_EQ(response.at("result").at("empty"), true);
}

TEST_F(JsonRpcServerTest, ReturnsInvalidRequestForHandlerArgumentError) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("needs_param", [](const json& params) {
        if (!params.contains("required")) {
            throw std::invalid_argument("required missing");
        }
        return json{{"ok", true}};
    });

    const json request = {
            {"jsonrpc", "2.0"},
            {"method", "needs_param"},
            {"params", json::object()},
            {"id", 8}
    };
    std::istringstream input(Frame(request));
    std::ostringstream output;

    StdioJsonRpcServer server(std::move(dispatcher), input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_EQ(response.at("error").at("code"), -32600);
    EXPECT_NE(response.at("error").at("message").get<std::string>().find("required missing"),
              std::string::npos);
}

TEST_F(JsonRpcServerTest, ReturnsParseErrorForInvalidJsonPayload) {
    const std::string payload = "{ invalid json";
    std::istringstream input("Content-Length: " + std::to_string(payload.size()) +
                             "\r\n\r\n" + payload);
    std::ostringstream output;

    StdioJsonRpcServer server(JsonRpcDispatcher{}, input, output);
    server.Run();

    const json response = ReadFramedMessage(output.str());
    EXPECT_TRUE(response.at("id").is_null());
    EXPECT_EQ(response.at("error").at("code"), -32700);
}
