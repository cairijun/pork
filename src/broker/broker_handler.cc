#include <cstring>
#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "broker/broker_handler.h"
#include "common.h"
#include "proto_types.h"

namespace pork {

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
        rlock_queues_mtx.unlock();

        if (q_iter == queues.end()) {
            PORK_LOCK(queues_mtx);
            q_iter = queues.find(queue_name);
            if (q_iter == queues.end()) {
                queues[queue_name] = create_mq();
            }
            q_iter = queues.find(queue_name);
        }

        return q_iter->second;
    }

    id_t BrokerHandler::get_next_id()
    {
        return next_id++;
    }

} /* pork  */
