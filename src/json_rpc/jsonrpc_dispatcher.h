/**
 * @file JsonRpcDispatcher.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef JSONRPCDISPATCHER_H
#define JSONRPCDISPATCHER_H
#include <functional>
#include "jsonrpc_types.h"


namespace mcp {
    namespace jsonrpc {
        class JsonRpcDispatcher {
        public:
            /**
             * @brief 接受参数，返回json结果
             */
            using handler = std::function<json(const json& params)>;

            void RegisterHandler(const std::string& method, handler handler);

            bool HasHandler(const std::string& method) const;

            json Call(const std::string& method, const json& params) const;
        private:
            /// 方法处理器映射，用来查询已经注册的方法
            std::unordered_map<std::string, handler> m_handlers_;
        };
    }
}



#endif //JSONRPCDISPATCHER_H
