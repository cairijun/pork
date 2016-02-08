#include <cstring>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "broker/broker_handler.h"
#include "common.h"
#include "proto_types.h"

namespace pork {

    BrokerHandler::BrokerHandler(zhandle_t* zk_handle):
        zk_handle(zk_handle)
    {
        char zk_node_path_buf[256];
        int ret = zoo_create(zk_handle, ZNODE_ID_BLOCK_PREFIX, NULL, -1,
                &ZOO_READ_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE,
                zk_node_path_buf, 256);
        if (ret != ZOK) {
            throw std::runtime_error(zerror(ret));
        }
        id_t block_id = boost::lexical_cast<id_t>(
                zk_node_path_buf + strlen(ZNODE_ID_BLOCK_PREFIX));
        next_id = 1 + (block_id << (sizeof(block_id) * 4));  // block id as the upper half
    }

    void BrokerHandler::getMessage(
            Message& _return,
            const std::string& queue_name,
            const id_t last_msg)
    {
        if (!ensure_queue(queue_name)->pop_free_message(_return)) {
            throw Timeout();  // no free msg
        }
    }

    id_t BrokerHandler::addMessage(
            const std::string& queue_name,
            const Message& message,
            const std::vector<Dependency>& deps)
    {
        auto msg = std::make_shared<Message>(message);
        msg->__set_id(get_next_id());
        ensure_queue(queue_name)->push_message(msg, deps);
        return msg->id;
    }

    void BrokerHandler::addMessageGroup(
            std::vector<id_t>& _return,
            const std::string& queue_name,
            const std::vector<Message>& messages,
            const std::vector<Dependency>& deps)
    {
        auto q = ensure_queue(queue_name);
        _return.clear();
        for (auto& m : messages) {
            auto msg = std::make_shared<Message>(m);
            msg->__set_id(get_next_id());
            q->push_message(msg, deps);
            _return.push_back(msg->id);
        }
    }

    void BrokerHandler::ack(const std::string& queue_name, const id_t msg_id)
    {
        ensure_queue(queue_name)->ack(msg_id);
    }

    void BrokerHandler::fail(const std::string& queue_name, const id_t msg_id)
    {
        ensure_queue(queue_name)->fail(msg_id);
    }

    std::shared_ptr<AbstractMessageQueue> BrokerHandler::create_mq() {
        return std::shared_ptr<MessageQueue>(new MessageQueue());
    }

    std::shared_ptr<AbstractMessageQueue> BrokerHandler::ensure_queue(
            const std::string& queue_name)
    {
        PORK_RLOCK(rlock_, queues_mtx);
        auto q_iter = queues.find(queue_name);
        if (q_iter == queues.end()) {
            PORK_RLOCK_UPGRADE(rlock_queues_mtx);
            queues[queue_name] = create_mq();
            q_iter = queues.find(queue_name);
        }
        return q_iter->second;
    }

    id_t BrokerHandler::get_next_id()
    {
        return next_id++;
    }

} /* pork  */
