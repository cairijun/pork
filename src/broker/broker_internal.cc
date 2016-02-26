#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>

#include "broker/broker_internal.h"
#include "common.h"

namespace pork {

    void BrokerInternalHandler::syncSnapshot(const SnapshotSdto& snapshot)
    {
        PORK_LOCK(queues_mtx);
        queues.clear();
        for (auto& sq : snapshot.queues) {
            queues[sq.first] = std::make_shared<MessageQueue>(sq.second);
        }
    }

    void BrokerInternalHandler::syncSetMessageState(
            const std::string& queue_name,
            const id_t msg_id,
            const MessageState::type state)
    {
        ensure_queue(queue_name)->set_msg_state(msg_id, state);
    }

    void BrokerInternalHandler::syncAddMessages(
            const std::string& queue_name,
            const std::vector<Message>& msgs,
            const std::vector<Dependency>& deps)
    {
        auto q = ensure_queue(queue_name);
        for (auto& m : msgs) {
            q->push_message(std::make_shared<Message>(m), deps);
        }
    }
}
