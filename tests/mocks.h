#include <vector>
#include <gmock/gmock.h>

#include "Broker.h"
#include "proto_types.h"

namespace pork {

    class MockBrokerIf: public BrokerIf {
        public:
            MOCK_METHOD3(getMessage, void(
                        Message& _return,
                        const std::string& queue_name,
                        const id_t last_msg));

            MOCK_METHOD3(addMessage, id_t(
                        const std::string& queue_name,
                        const Message& message,
                        const std::vector<Dependency>& deps));

            MOCK_METHOD4(addMessageGroup, void(
                        std::vector<id_t>& _return,
                        const std::string& queue_name,
                        const std::vector<Message>& messages,
                        const std::vector<Dependency>& deps));

            MOCK_METHOD2(ack, void(const std::string& queue_name, const id_t msg_id));

            MOCK_METHOD2(fail, void(const std::string& queue_name, const id_t msg_id));
    };

}
