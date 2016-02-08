#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "broker/broker_handler.h"
#include "mocks.h"
#include "proto_types.h"

using namespace testing;

namespace pork {

    class TestingBrokerHandler: public BrokerHandler {
        public:
            TestingBrokerHandler(): BrokerHandler() {}

            std::shared_ptr<FakeMessageQueue> get_mq(const std::string& queue_name) {
                return std::dynamic_pointer_cast<FakeMessageQueue>(queues.at(queue_name));
            }

            std::shared_ptr<FakeMessageQueue> create_and_insert_mq(
                    const std::string& queue_name) {
                std::shared_ptr<FakeMessageQueue> mq(new FakeMessageQueue());
                queues[queue_name] = mq;
                return mq;
            }

        protected:
            std::shared_ptr<AbstractMessageQueue> create_mq() override {
                return std::shared_ptr<FakeMessageQueue>(new FakeMessageQueue());
            }
    };

    Message create_msg(const std::string& payload, id_t id = -1) {
        Message msg;
        if (id != -1) {
            msg.__set_id(id);
        }
        msg.payload = payload;
        msg.type = MessageType::NORMAL;
        return msg;
    }

    Dependency create_dep(const std::string& key, int n) {
        Dependency dep;
        dep.key = key;
        dep.n = n;
        return dep;
    }

    TEST(BrokerHandlerTest, GetFreeMsgs) {
        TestingBrokerHandler h;
        Message recv;
        EXPECT_THROW(h.getMessage(recv, "q", 0), Timeout);

        auto mq = h.get_mq("q");
        int n_msgs = 10;
        std::vector<Message> msgs;
        for (int i = 0; i < n_msgs; ++i) {
            auto msg = create_msg(std::to_string(i), i);
            msgs.push_back(msg);
            mq->free_msgs.push_back(msg);
        }

        for (auto m : msgs) {
            h.getMessage(recv, "q", 0);
            EXPECT_EQ(m, recv);
        }

        EXPECT_THROW(h.getMessage(recv, "q", 0), Timeout);
    }

    void TestAddMsgs(bool group) {
        TestingBrokerHandler h;

        auto msg1 = create_msg("m1");
        auto msg2 = create_msg("m2");
        auto dep1 = create_dep("dep1", 1);
        auto dep2 = create_dep("dep2", 2);
        auto dep3 = create_dep("dep3", 3);

        id_t id1, id2;

        if (group) {
            std::vector<id_t> ids;
            h.addMessageGroup(ids, "q", {msg1, msg2}, {dep1, dep2, dep3});
            EXPECT_THAT(ids, SizeIs(2));
            id1 = ids[0];
            id2 = ids[1];
        } else {
            id1 = h.addMessage("q", msg1, {dep1, dep2});
            id2 = h.addMessage("q", msg2, {dep2, dep3});
        }

        EXPECT_GT(id2, id1);

        auto mq = h.get_mq("q");
        auto pushed_1 = mq->pushed_msgs[0];
        auto pushed_2 = mq->pushed_msgs[1];
        auto pushed_msg1 = std::get<0>(pushed_1);
        auto pushed_msg2 = std::get<0>(pushed_2);
        auto pushed_deps1 = std::get<1>(pushed_1);
        auto pushed_deps2 = std::get<1>(pushed_2);

        msg1.__set_id(id1);
        msg2.__set_id(id2);

        EXPECT_EQ(msg1, *pushed_msg1);
        EXPECT_EQ(msg2, *pushed_msg2);

        if (group) {
            EXPECT_THAT(pushed_deps1, ElementsAre(dep1, dep2, dep3));
            EXPECT_THAT(pushed_deps2, ElementsAre(dep1, dep2, dep3));
        } else {
            EXPECT_THAT(pushed_deps1, ElementsAre(dep1, dep2));
            EXPECT_THAT(pushed_deps2, ElementsAre(dep2, dep3));
        }
    }

    TEST(BrokerHandlerTest, AddMsgs) {
        TestAddMsgs(false);
    }

    TEST(BrokerHandlerTest, AddMsgGroup) {
        TestAddMsgs(true);
    }

    TEST(BrokerHandlerTest, Ack) {
        TestingBrokerHandler h;
        h.ack("q", 1);
        EXPECT_THAT(h.get_mq("q")->acked_msgs, ElementsAre(1));
    }

    TEST(BrokerHandlerTest, Fail) {
        TestingBrokerHandler h;
        h.fail("q", 1);
        EXPECT_THAT(h.get_mq("q")->failed_msgs, ElementsAre(1));
    }

} /* pork */
