#ifndef BROKER_HANDLER_H_S74ILY2B
#define BROKER_HANDLER_H_S74ILY2B

#include <atomic>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include <boost/thread/shared_mutex.hpp>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "broker/message_queue.h"
#include "proto_types.h"

namespace pork {

    class BrokerHandler: virtual public BrokerIf {
        public:
            BrokerHandler(id_t next_id = 0): next_id(next_id) {}
            BrokerHandler(const BrokerHandler&) = delete;
            void getMessage(
                    Message& _return,
                    const std::string& queue_name,
                    const id_t last_msg) override;
            id_t addMessage(
                    const std::string& queue_name,
                    const Message& message,
                    const std::vector<Dependency>& deps) override;
            void addMessageGroup(
                    std::vector<id_t>& _return,
                    const std::string& queue_name,
                    const std::vector<Message>& messages,
                    const std::vector<Dependency>& deps) override;
            void ack(const std::string& queue_name, const id_t msg_id) override;
            void fail(const std::string& queue_name, const id_t msg_id) override;

        protected:
            virtual std::shared_ptr<AbstractMessageQueue> create_mq();

            std::unordered_map<std::string, std::shared_ptr<AbstractMessageQueue>> queues;
            boost::upgrade_mutex queues_mtx;
            std::atomic<id_t> next_id;

            std::shared_ptr<AbstractMessageQueue> ensure_queue(
                    const std::string& queue_name);
            id_t get_next_id();

            zhandle_t* zk_handle;
    };

} /* pork  */


#endif /* end of include guard: BROKER_HANDLER_H_S74ILY2B */
