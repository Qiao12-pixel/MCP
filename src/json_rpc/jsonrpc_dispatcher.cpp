/**
 * @file JsonRpcDispatcher.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "jsonrpc_dispatcher.h"

#include "logger/logger.h"

namespace mcp {
    namespace jsonrpc {
        JsonRpcDispatcher::JsonRpcDispatcher(size_t thread_count, size_t max_queue_size) {
            EnableThreadPool(thread_count, max_queue_size);
        }

        void JsonRpcDispatcher::RegisterHandler(const std::string &method, handler handler) {
            m_handlers_[method] = std::move(handler);
        }

        bool JsonRpcDispatcher::HasHandler(const std::string &method) const {
            return m_handlers_.find(method) != m_handlers_.end();
        }

        json JsonRpcDispatcher::Call(const std::string &method, const json &params) const {
            auto it = m_handlers_.find(method);
            if (it == m_handlers_.end()) {
                throw std::runtime_error("Unknown method: " + method);
            }

            const auto& registered_handler = it->second;
            if (m_thread_pool_ && ShouldRunInThreadPool(method)) {
                MCP_LOG_INFO("Dispatching method '{}' in thread pool", method);
                auto future = m_thread_pool_->Submit([registered_handler, params] {
                    return registered_handler(params);
                });
                return future.get();
            }

            return registered_handler(params);
        }

        void JsonRpcDispatcher::EnableThreadPool(size_t thread_count, size_t max_queue_size) {
            m_thread_pool_ = std::make_shared<ThreadPool>(thread_count, max_queue_size);
        }

        bool JsonRpcDispatcher::ShouldRunInThreadPool(const std::string &method) const {
            return method == "tools/call" ||
                   method == "resources/read" ||
                   method == "prompts/get";
        }
    }
}
