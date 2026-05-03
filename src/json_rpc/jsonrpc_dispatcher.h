/**
 * @file JsonRpcDispatcher.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef JSONRPCDISPATCHER_H
#define JSONRPCDISPATCHER_H
#include <functional>
#include <memory>
#include <unordered_map>
#include "jsonrpc_types.h"
#include "utils/thread_pool.h"


namespace mcp {
    namespace jsonrpc {
        class JsonRpcDispatcher {
        public:
            /**
             * @brief 接受参数，返回json结果
             */
            using handler = std::function<json(const json& params)>;

            JsonRpcDispatcher() = default;
            JsonRpcDispatcher(size_t thread_count, size_t max_queue_size);

            void RegisterHandler(const std::string& method, handler handler);

            bool HasHandler(const std::string& method) const;

            json Call(const std::string& method, const json& params) const;

            void EnableThreadPool(size_t thread_count, size_t max_queue_size);
        private:
            bool ShouldRunInThreadPool(const std::string& method) const;

            /// 方法处理器映射，用来查询已经注册的方法
            std::unordered_map<std::string, handler> m_handlers_;
            std::shared_ptr<ThreadPool> m_thread_pool_;
        };
    }
}



#endif //JSONRPCDISPATCHER_H
