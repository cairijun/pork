#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <boost/chrono.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "broker/message_queue.h"
#include "internal_types.h"
#include "proto_types.h"

using namespace testing;

MATCHER_P(WithKeyIsValueOf, map, "") {
    return get<0>(arg) == map.at(get<1>(arg));
}

namespace pork {

    class BrokerMqTest: public Test {
        protected:
            void SetUp() override {
                // speed up the tests
                MessageQueue::POP_FREE_TIMEOUT = boost::chrono::milliseconds(50);
                mq.reset(new MessageQueue());
            }

            static Dependency make_dep(const std::string& key, int n) {
                Dependency dep;
                dep.key = key;
                dep.n = n;
                return dep;
            }

            static DependencySdto make_dep_sdto(
                    int n_resolved, const std::vector<id_t> dependants) {
                DependencySdto sdto;
                sdto.n_resolved = n_resolved;
                sdto.dependant_ids = dependants;
                return sdto;
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

            static MessageSdto make_msg_sdto(
                    const Message& msg, MessageState::type state, int n_deps) {
                MessageSdto sdto;
                sdto.msg = msg;
                sdto.state = state;
                sdto.n_deps = n_deps;
                return sdto;
            }

            decltype(MessageQueue::free_msgs)& get_free_msgs() {
                return mq->free_msgs;
            }

            decltype(MessageQueue::all_msgs)& get_all_msgs() {
                return mq->all_msgs;
            }

            decltype(MessageQueue::all_deps)& get_all_deps() {
                return mq->all_deps;
            }

            bool is_serving() const {
                return mq->is_serving;
            }

            std::unique_ptr<MessageQueue> mq;
    };

    struct SyncOperation {
        virtual void apply(const std::unique_ptr<MessageQueue>& mq) = 0;
    };

    struct SetState: SyncOperation {
        id_t id;
        MessageState::type state;
        SetState(id_t id, MessageState::type state): id(id), state(state) {}

        void apply(const std::unique_ptr<MessageQueue>& mq) override {
            mq->set_msg_state(id, state);
        }
    };

    struct AddMessage: SyncOperation {
        void apply(const std::unique_ptr<MessageQueue>& mq) override {
        }
    };

    TEST_F(BrokerMqTest, PopTimeout) {
        std::vector<std::thread> ts;
        for (int i = 0; i < 20; ++i) {
            ts.emplace_back([this] () {
                Message msg;
                EXPECT_FALSE(mq->pop_free_message(msg));
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
                        mq->push_message(make_msg(id), {});
                    }
                }
            });
        }

        for (int i = 0; i < n_consumers; ++i) {
            ts.emplace_back([this, n_msgs, &n_recv] () {
                while (n_recv < n_msgs) {
                    Message msg;
                    if (mq->pop_free_message(msg)) {
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

        mq->push_message(msg11, {});
        mq->pop_free_message(recv);
        mq->push_message(msg21, {});
        mq->pop_free_message(recv);
        mq->push_message(msg22, {});
        mq->pop_free_message(recv);
        mq->push_message(msg31, {});
        mq->pop_free_message(recv);

        mq->push_message(msg1, {make_dep("dep1", 1), make_dep("dep2", 2)});
        EXPECT_FALSE(mq->pop_free_message(recv));

        mq->ack(msg11->id);

        mq->push_message(msg2, {make_dep("dep1", 1), make_dep("dep2", 1)});
        EXPECT_FALSE(mq->pop_free_message(recv));

        mq->ack(msg31->id);

        mq->push_message(msg3, {make_dep("dep1", 1), make_dep("dep3", 1)});
        EXPECT_TRUE(mq->pop_free_message(recv));
        EXPECT_EQ(*msg3, recv);
        EXPECT_FALSE(mq->pop_free_message(recv));

        mq->ack(msg21->id);

        EXPECT_TRUE(mq->pop_free_message(recv));
        EXPECT_EQ(*msg2, recv);
        EXPECT_FALSE(mq->pop_free_message(recv));

        mq->ack(msg22->id);

        EXPECT_TRUE(mq->pop_free_message(recv));
        EXPECT_EQ(*msg1, recv);
        EXPECT_FALSE(mq->pop_free_message(recv));
    }

    TEST_F(BrokerMqTest, AckNonInProgressMsgs) {
        Message recv;

        auto m_queuing = make_msg(1, "dep");
        auto m_failed = make_msg(2, "dep");
        auto m_acked = make_msg(3, "dep");
        auto m_in_progress = make_msg(4, "dep");
        auto msg = make_msg(5);

        mq->push_message(m_queuing, {make_dep("impossible", 1)});

        mq->push_message(m_failed, {});
        mq->pop_free_message(recv);
        mq->fail(m_failed->id);

        mq->push_message(m_acked, {});
        mq->pop_free_message(recv);
        mq->ack(m_acked->id);

        mq->push_message(m_in_progress, {});
        mq->pop_free_message(recv);

        mq->push_message(msg, {make_dep("dep", 2)});

        mq->ack(m_queuing->id);
        mq->ack(m_failed->id);
        mq->ack(m_acked->id);

        EXPECT_FALSE(mq->pop_free_message(recv));

        mq->ack(m_in_progress->id);

        EXPECT_TRUE(mq->pop_free_message(recv));
        EXPECT_EQ(*msg, recv);
    }

    TEST_F(BrokerMqTest, FakeWorkLoad) {
        int n_msgs = 0;
        int n_groups = 500;
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
            int x = n - exp_dist(rng);
            if (x < 0) {
                x = 0;
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
                        mq->push_message(msg, {});
                    } else {
                        Dependency dep;
                        dep.key = "dep" + std::to_string(group_id - 1);
                        dep.n = group_size;
                        mq->push_message(msg, {dep});
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
                    if (mq->pop_free_message(recv)) {
                        ++n_recv;
                        int group_id = recv.id / group_size;
                        if (group_id != 0) {
                            EXPECT_EQ(group_size, ack_count[group_id - 1]);
                        }
                        ++ack_count[group_id];
                        mq->ack(recv.id);
                    }
                }
            });
        }

        for (auto& t : ts) {
            t.join();
        }

        delete[] ack_count;
    }


    // tests for synchronization
    TEST_F(BrokerMqTest, InitWithSnapshot) {
        QueueSdto sdto;
        sdto.all_msgs = {
            {1, make_msg_sdto(*make_msg(1, "depA"), MessageState::ACKED, 0)},
            {2, make_msg_sdto(*make_msg(2, "depB"), MessageState::IN_PROGRESS, 0)},
            {3, make_msg_sdto(*make_msg(3, "depB"), MessageState::QUEUING, 1)},
            {4, make_msg_sdto(*make_msg(4), MessageState::QUEUING, 2)},
            {5, make_msg_sdto(*make_msg(5), MessageState::QUEUING, 0)},
            {6, make_msg_sdto(*make_msg(6), MessageState::FAILED, 0)},
        };
        sdto.all_deps = {
            {"depA", make_dep_sdto(1, {3, 4})},
            {"depB", make_dep_sdto(0, {4})},
        };

        mq.reset(new MessageQueue(sdto));

        EXPECT_FALSE(is_serving());

        auto& all_msgs = get_all_msgs();
        auto& all_deps = get_all_deps();

        EXPECT_THAT(all_msgs, SizeIs(sdto.all_msgs.size()));
        for (auto& expected : sdto.all_msgs) {
            auto& actual = all_msgs[expected.first];
            EXPECT_EQ(expected.second.msg, *actual->msg);
            EXPECT_EQ(expected.second.state, actual->state);
            EXPECT_EQ(expected.second.n_deps, actual->n_deps);
        }

        EXPECT_THAT(all_deps, SizeIs(sdto.all_deps.size()));
        for (auto& expected : sdto.all_deps) {
            auto& actual = all_deps[expected.first];
            EXPECT_EQ(expected.second.n_resolved, actual->n_resolved);
            EXPECT_THAT(actual->dependants, Pointwise(WithKeyIsValueOf(all_msgs),
                        expected.second.dependant_ids));
        }

        mq->start_serving();

        EXPECT_TRUE(is_serving());
        EXPECT_THAT(get_free_msgs(), ElementsAre(all_msgs[5]));
    }

    TEST_F(BrokerMqTest, Sync) {
        mq.reset(new MessageQueue(QueueSdto()));

        std::vector<std::shared_ptr<Message>> msgs;
        std::vector<std::function<void()>> operations;

        for (int i = 0; i < 5000; ++i) {
            auto msg = make_msg(i, std::to_string(i / 4));
            msgs.push_back(msg);
            if (i < 4) {
                operations.push_back([i, msg, this] () {
                    mq->push_message(msg, {});
                });
            } else {
                operations.push_back([i, msg, this] () {
                    mq->push_message(msg, {make_dep(std::to_string(i / 4 - 1), 4)});
                });
            }
            if (i % 20 == 0) {
                operations.push_back([i, this] () {
                    mq->set_msg_state(i, MessageState::FAILED);
                });
            }
            operations.push_back([i, this] () {
                mq->set_msg_state(i, MessageState::IN_PROGRESS);
            });
            operations.push_back([i, this] () {
                mq->set_msg_state(i, MessageState::ACKED);
            });
        }

        auto waiting_msg = make_msg(10000);
        operations.push_back([waiting_msg, this] () {
            mq->push_message(waiting_msg, {make_dep("unresolved", 2)});
        });
        msgs.push_back(waiting_msg);

        auto free_msg = make_msg(20000);
        operations.push_back([free_msg, this] () {
            mq->push_message(free_msg, {});
        });
        msgs.push_back(free_msg);

        std::mt19937 rng((std::random_device())());
        std::shuffle(operations.begin(), operations.end(), rng);

        std::vector<std::thread> ts;
        std::atomic_size_t idx(0);
        for (int i = 0; i < 5; ++i) {
            ts.emplace_back([&] () {
                size_t idx_local = idx++;
                while (idx_local < operations.size()) {
                    operations[idx_local]();
                    idx_local = idx++;
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }

        mq->start_serving();

        auto& all_msgs = get_all_msgs();
        EXPECT_THAT(all_msgs, SizeIs(all_msgs.size()));
        for (auto& m : msgs) {
            auto& actual = all_msgs[m->id];
            EXPECT_EQ(m, actual->msg);
            if (m != waiting_msg) {
                EXPECT_EQ(0, actual->n_deps);
            }
            if (m != waiting_msg && m != free_msg) {
                EXPECT_EQ(MessageState::ACKED, actual->state);
            }
        }

        auto& all_deps = get_all_deps();
        EXPECT_THAT(all_deps, SizeIs(all_msgs.size() / 4 + 1));
        for (int i = 0; i < all_msgs.size() / 4; ++i) {
            auto& dep = all_deps[std::to_string(i)];
            EXPECT_EQ(4, dep->n_resolved);
            EXPECT_THAT(dep->dependants, IsEmpty());
        }
        EXPECT_EQ(0, all_deps["unresolved"]->n_resolved);
        EXPECT_THAT(all_deps["unresolved"]->dependants,
                ElementsAre(all_msgs[waiting_msg->id]));

        EXPECT_EQ(2, all_msgs[waiting_msg->id]->n_deps);
        EXPECT_EQ(MessageState::QUEUING, all_msgs[waiting_msg->id]->state);

        EXPECT_EQ(MessageState::QUEUING, all_msgs[free_msg->id]->state);

        EXPECT_THAT(get_free_msgs(), ElementsAre(all_msgs[free_msg->id]));
    }
}
