/**
 * @file JsonRpcDispatcher.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "jsonrpc_dispatcher.h"
namespace mcp {
    namespace jsonrpc {
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
            return it->second(params);
        }
    }
}