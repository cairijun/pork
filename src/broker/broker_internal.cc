#include "broker/broker_internal.h"

namespace pork {

    void BrokerInternalHandler::syncSnapshot(const SnapshotSdto& op)
    {
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
