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
    }

    void BrokerInternalHandler::syncAddMessages(
            const std::string& queue_name,
            const std::vector<Message>& msgs,
            const std::vector<Dependency>& deps)
    {
    }
}
