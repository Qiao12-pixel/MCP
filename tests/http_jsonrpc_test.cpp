#include <gtest/gtest.h>

#include <logger/logger.h>
#include <json_rpc/http_jsonrpc.h>

using namespace mcp::jsonrpc;

class HttpJsonRpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
        mcp::logger::Logger::GetInstance().Init("http_jsonrpc_test", "", 1024 * 1024, 3, false);
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }

    static HttpJsonRpcServer MakeServer(JsonRpcDispatcher dispatcher = JsonRpcDispatcher{}) {
        return HttpJsonRpcServer(std::move(dispatcher), "127.0.0.1", 0);
    }
};

TEST_F(HttpJsonRpcServerTest, ConstructorInitializesServerAndExposesAddress) {
    auto server = MakeServer();

    EXPECT_EQ(server.GetHost(), "127.0.0.1");
    EXPECT_EQ(server.GetPort(), 0);
    EXPECT_FALSE(server.IsRunning());
}

TEST_F(HttpJsonRpcServerTest, HandlesSingleRequest) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("add", [](const nlohmann::json& params) {
        return params.at("a").get<int>() + params.at("b").get<int>();
    });
    auto server = MakeServer(std::move(dispatcher));

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "add"},
        {"params", {{"a", 2}, {"b", 3}}}
    };

    auto response = nlohmann::json::parse(server.HandleRequest(request.dump()));

    EXPECT_EQ(response.at("jsonrpc"), "2.0");
    EXPECT_EQ(response.at("id"), 1);
    EXPECT_EQ(response.at("result"), 5);
}

TEST_F(HttpJsonRpcServerTest, HandlesNotificationWithoutResponseBody) {
    bool called = false;
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("notify", [&](const nlohmann::json& params) {
        called = params.at("ok").get<bool>();
        return nlohmann::json{{"ignored", true}};
    });
    auto server = MakeServer(std::move(dispatcher));

    nlohmann::json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notify"},
        {"params", {{"ok", true}}}
    };

    EXPECT_TRUE(server.HandleRequest(notification.dump()).empty());
    EXPECT_TRUE(called);
}

TEST_F(HttpJsonRpcServerTest, ReturnsMethodNotFound) {
    auto server = MakeServer();

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", "missing"},
        {"method", "missing"}
    };

    auto response = nlohmann::json::parse(server.HandleRequest(request.dump()));

    EXPECT_EQ(response.at("id"), "missing");
    EXPECT_EQ(response.at("error").at("code"), jsonrpc_error_code::MethodNotFound);
}

TEST_F(HttpJsonRpcServerTest, ReturnsParseErrorForInvalidJson) {
    auto server = MakeServer();

    auto response = nlohmann::json::parse(server.HandleRequest("{bad json"));

    EXPECT_EQ(response.at("jsonrpc"), "2.0");
    EXPECT_TRUE(response.at("id").is_null());
    EXPECT_EQ(response.at("error").at("code"), jsonrpc_error_code::ParseError);
}

TEST_F(HttpJsonRpcServerTest, HandlesBatchAndSkipsNotifications) {
    bool notified = false;
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("echo", [](const nlohmann::json& params) {
        return params;
    });
    dispatcher.RegisterHandler("notify", [&](const nlohmann::json&) {
        notified = true;
        return nlohmann::json(nullptr);
    });
    auto server = MakeServer(std::move(dispatcher));

    nlohmann::json batch = nlohmann::json::array({
        {
            {"jsonrpc", "2.0"},
            {"id", 7},
            {"method", "echo"},
            {"params", {{"value", "pong"}}}
        },
        {
            {"jsonrpc", "2.0"},
            {"method", "notify"}
        }
    });

    auto response = nlohmann::json::parse(server.HandleRequest(batch.dump()));

    ASSERT_TRUE(response.is_array());
    ASSERT_EQ(response.size(), 1);
    EXPECT_EQ(response.at(0).at("id"), 7);
    EXPECT_EQ(response.at(0).at("result").at("value"), "pong");
    EXPECT_TRUE(notified);
}
