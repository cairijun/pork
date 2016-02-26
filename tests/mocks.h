#include <deque>
#include <memory>
#include <tuple>
#include <vector>

#include <gmock/gmock.h>

#include "Broker.h"
#include "broker/message_queue.h"
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

    class FakeMessageQueue: public AbstractMessageQueue {
        public:
            bool pop_free_message(Message& msg) override {
                if (free_msgs.empty()) {
                    return false;
                }
                msg = free_msgs.front();
                free_msgs.pop_front();
                return true;
            }

            void push_message(
                    const std::shared_ptr<Message>& msg,
                    const std::vector<Dependency>& deps) override {
                pushed_msgs.emplace_back(msg, deps);
            }

            void ack(id_t msg_id) override {
                acked_msgs.push_back(msg_id);
            }

            void fail(id_t msg_id) override {
                failed_msgs.push_back(msg_id);
            }

            void set_msg_state(id_t msg_id, MessageState::type state) override {
                throw "Not implemented yet!";
            }

            std::deque<Message> free_msgs;
            std::deque<std::tuple<const std::shared_ptr<Message>,
                                  const std::vector<Dependency>>> pushed_msgs;
            std::deque<id_t> acked_msgs;
            std::deque<id_t> failed_msgs;
    };

}
