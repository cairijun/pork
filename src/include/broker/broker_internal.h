#include <string>
#include <vector>

#include <BrokerInternal.h>

#include "broker_handler.h"

namespace pork {
    class BrokerInternalHandler: public BrokerInternalIf, public BrokerHandler {
        public:
            using BrokerHandler::BrokerHandler;
            void syncSnapshot(const SnapshotSdto& op) override;
            void syncSetMessageState(
                    const std::string& queue_name,
                    const id_t msg_id,
                    const MessageState::type state) override;
            void syncAddMessages(
                    const std::string& queue_name,
                    const std::vector<Message>& msgs,
                    const std::vector<Dependency>& deps) override;
    };
};
