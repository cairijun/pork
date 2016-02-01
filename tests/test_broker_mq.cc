#include <algorithm>
#include <atomic>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <boost/chrono.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "broker/message_queue.h"
#include "proto_types.h"

using namespace testing;

namespace pork {

    class BrokerMqTest: public Test {
        protected:
            void SetUp() override {
                // speed up the tests
                MessageQueue::POP_FREE_TIMEOUT = boost::chrono::milliseconds(50);
            }

            static Dependency make_dep(const std::string& key, int n) {
                Dependency dep;
                dep.key = key;
                dep.n = n;
                return dep;
            }

            static std::shared_ptr<Message> make_msg(
                    id_t id, const std::string& resolve_dep = "") {
                auto msg = std::make_shared<Message>();
                msg->__set_id(id);
                msg->payload = "msg" + std::to_string(id);
                msg->type = MessageType::NORMAL;
                if (!resolve_dep.empty()) {
                    msg->__set_resolve_dep(resolve_dep);
                }
                return msg;
            }

            MessageQueue mq;
    };

    TEST_F(BrokerMqTest, PopTimeout) {
        std::vector<std::thread> ts;
        for (int i = 0; i < 20; ++i) {
            ts.emplace_back([this] () {
                Message msg;
                EXPECT_FALSE(mq.pop_free_message(msg));
            });
        }
        for (auto& t : ts) {
            t.join();
        }
    }

    TEST_F(BrokerMqTest, PushPopFreeMsgs) {
        int n_msgs = 2000;
        int n_producers = 5;
        int n_consumers = 10;

        std::vector<std::thread> ts;
        std::atomic_int n_sent(0);
        std::atomic_int n_recv(0);

        for (int i = 0; i < n_producers; ++i) {
            ts.emplace_back([this, n_msgs, &n_sent] () {
                while (n_sent < n_msgs) {
                    int id = ++n_sent;
                    if (id <= n_msgs) {
                        mq.push_message(make_msg(id), {});
                    }
                }
            });
        }

        for (int i = 0; i < n_consumers; ++i) {
            ts.emplace_back([this, n_msgs, &n_recv] () {
                while (n_recv < n_msgs) {
                    Message msg;
                    if (mq.pop_free_message(msg)) {
                        ++n_recv;
                        EXPECT_EQ(msg.payload, "msg" + std::to_string(msg.id));
                    }
                }
            });
        }

        for (auto& t : ts) {
            t.join();
        }
    }

    TEST_F(BrokerMqTest, MsgsWithDeps) {
        Message recv;

        auto msg11 = make_msg(11, "dep1");
        auto msg21 = make_msg(12, "dep2");
        auto msg22 = make_msg(21, "dep2");
        auto msg31 = make_msg(31, "dep3");

        auto msg1 = make_msg(1);
        auto msg2 = make_msg(2);
        auto msg3 = make_msg(3);

        mq.push_message(msg11, {});
        mq.pop_free_message(recv);
        mq.push_message(msg21, {});
        mq.pop_free_message(recv);
        mq.push_message(msg22, {});
        mq.pop_free_message(recv);
        mq.push_message(msg31, {});
        mq.pop_free_message(recv);

        mq.push_message(msg1, {make_dep("dep1", 1), make_dep("dep2", 2)});
        EXPECT_FALSE(mq.pop_free_message(recv));

        mq.ack(msg11->id);

        mq.push_message(msg2, {make_dep("dep1", 1), make_dep("dep2", 1)});
        EXPECT_FALSE(mq.pop_free_message(recv));

        mq.ack(msg31->id);

        mq.push_message(msg3, {make_dep("dep1", 1), make_dep("dep3", 1)});
        EXPECT_TRUE(mq.pop_free_message(recv));
        EXPECT_EQ(*msg3, recv);
        EXPECT_FALSE(mq.pop_free_message(recv));

        mq.ack(msg21->id);

        EXPECT_TRUE(mq.pop_free_message(recv));
        EXPECT_EQ(*msg2, recv);
        EXPECT_FALSE(mq.pop_free_message(recv));

        mq.ack(msg22->id);

        EXPECT_TRUE(mq.pop_free_message(recv));
        EXPECT_EQ(*msg1, recv);
        EXPECT_FALSE(mq.pop_free_message(recv));
    }

    TEST_F(BrokerMqTest, AckNonInProgressMsgs) {
        Message recv;

        auto m_queuing = make_msg(1, "dep");
        auto m_failed = make_msg(2, "dep");
        auto m_acked = make_msg(3, "dep");
        auto m_in_progress = make_msg(4, "dep");
        auto msg = make_msg(5);

        mq.push_message(m_queuing, {make_dep("impossible", 1)});

        mq.push_message(m_failed, {});
        mq.pop_free_message(recv);
        mq.fail(m_failed->id);

        mq.push_message(m_acked, {});
        mq.pop_free_message(recv);
        mq.ack(m_acked->id);

        mq.push_message(m_in_progress, {});
        mq.pop_free_message(recv);

        mq.push_message(msg, {make_dep("dep", 2)});

        mq.ack(m_queuing->id);
        mq.ack(m_failed->id);
        mq.ack(m_acked->id);

        EXPECT_FALSE(mq.pop_free_message(recv));

        mq.ack(m_in_progress->id);

        EXPECT_TRUE(mq.pop_free_message(recv));
        EXPECT_EQ(*msg, recv);
    }

    TEST_F(BrokerMqTest, FakeWorkLoad) {
        int n_msgs = 0;
        int n_groups = 100;
        int group_size = 10;
        std::vector<std::shared_ptr<Message>> msgs;
        for (int i = 0; i < n_groups; ++i) {
            for (int j = 0; j < group_size; ++j) {
                msgs.push_back(make_msg(n_msgs++, "dep" + std::to_string(i)));
            }
        }

        std::mt19937 rng((std::random_device())());
        std::exponential_distribution<double> exp_dist(0.2);
        std::random_shuffle(msgs.begin(), msgs.end(), [&rng, &exp_dist] (int n) {
            int x = exp_dist(rng);
            if (x >= n) {
                x = n - 1;
            }
            return x;
        });

        std::vector<std::thread> ts;

        std::atomic_int idx(-1);
        int n_producers = 5;
        for (int i = 0; i < n_producers; ++i) {
            ts.emplace_back([&] () {
                int local_idx;
                while (idx < n_msgs) {
                    local_idx = ++idx;
                    if (local_idx >= n_msgs) {
                        break;
                    }
                    auto msg = msgs[local_idx];
                    int group_id = msg->id / group_size;
                    if (group_id == 0) {  // first group
                        mq.push_message(msg, {});
                    } else {
                        Dependency dep;
                        dep.key = "dep" + std::to_string(group_id - 1);
                        dep.n = group_size;
                        mq.push_message(msg, {dep});
                    }
                }
            });
        }

        auto* ack_count = new std::atomic_int[n_groups];
        for (int i = 0; i < n_groups; ++i) {
            ack_count[i] = 0;
        }

        int n_consumers = 10;
        std::atomic_int n_recv(0);
        for (int i = 0; i < n_consumers; ++i) {
            ts.emplace_back([&] () {
                while (n_recv < n_msgs) {
                    Message recv;
                    if (mq.pop_free_message(recv)) {
                        ++n_recv;
                        int group_id = recv.id / group_size;
                        if (group_id != 0) {
                            EXPECT_EQ(group_size, ack_count[group_id - 1]);
                        }
                        ++ack_count[group_id];
                        mq.ack(recv.id);
                    }
                }
            });
        }

        for (auto& t : ts) {
            t.join();
        }

        delete[] ack_count;
    }
}
