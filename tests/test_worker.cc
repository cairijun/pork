#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks.h"
#include "worker.h"
#include "proto_types.h"

using namespace testing;

ACTION_P(Increase, p_counter)
{
    ++*p_counter;
}

namespace pork {

    class TestingWorker;

    typedef std::function<bool(const Message&)> SimpleProcFunc;
    typedef std::function<bool(const Message&, TestingWorker*)> ProcFunc;

    class TestingWorker: public BaseWorker {
        public:
            TestingWorker(
                    const ProcFunc& f,
                    const std::string& queue_name,
                    const std::shared_ptr<MockBrokerIf>& mock_broker_fetch,
                    const std::shared_ptr<MockBrokerIf>& mock_broker_process):
                f(f), BaseWorker(queue_name, mock_broker_fetch, mock_broker_process)
            {}

            TestingWorker(
                    const SimpleProcFunc& sf,
                    const std::string& queue_name,
                    const std::shared_ptr<MockBrokerIf>& mock_broker_fetch,
                    const std::shared_ptr<MockBrokerIf>& mock_broker_process):
                TestingWorker(
                        [sf] (const Message& msg, TestingWorker*) { return sf(msg); },
                        queue_name, mock_broker_fetch, mock_broker_process)
            {}

            using BaseWorker::emit;

        protected:
            bool process_message(const Message& msg)
            {
                return f(msg, this);
            }

        private:
            ProcFunc f;
    };

    class WorkerTest: public testing::Test {
        protected:
            void SetUp() override
            {
                last_msg_id = -1;
                mock_broker_fetch.reset(new MockBrokerIf());
                mock_broker_process.reset(new MockBrokerIf());

                ON_CALL(*mock_broker_fetch, getMessage(_, queue_name, _))
                    .WillByDefault(Throw(Timeout()));
            }

            void TearDown() override
            {
                mock_broker_fetch.reset();
                mock_broker_process.reset();
            }

            template<typename T>
            std::shared_ptr<TestingWorker> get_worker(
                    const std::string& queue_name, const T& f)
            {
                return std::make_shared<TestingWorker>(
                        f, queue_name, mock_broker_fetch, mock_broker_process);
            }

            Message create_msg(
                    const std::string& payload,
                    const std::string& resolved = "",
                    id_t id = -1,
                    MessageType::type type = MessageType::NORMAL)
            {
                Message msg;
                msg.__set_id(id == -1 ? ++last_msg_id : id);
                msg.type = type;
                msg.payload = payload;
                if (!resolved.empty()) {
                    msg.__set_resolve_dep(resolved);
                }
                return msg;
            }

            static int get_worker_buf_lwm()
            {
                return BaseWorker::buf_low_water_mark;
            }

            id_t last_msg_id;
            std::shared_ptr<MockBrokerIf> mock_broker_fetch;
            std::shared_ptr<MockBrokerIf> mock_broker_process;

            static const std::string queue_name;
    };

    const std::string WorkerTest::queue_name = "test_queue";


    TEST_F(WorkerTest, Process)
    {
        int n_msgs = 10;
        std::atomic_int n_acked(0);
        std::list<Message> msgs;
        Sequence deliver_seq;
        for (int i = 0; i < n_msgs; ++i) {
            auto msg = create_msg("message" + std::to_string(i));
            msgs.push_back(msg);
            InSequence s;
            EXPECT_CALL(*mock_broker_fetch, getMessage(_, queue_name, msg.id - 1))
                .InSequence(deliver_seq)
                .WillOnce(SetArgReferee<0>(msg));
            EXPECT_CALL(*mock_broker_process, ack(queue_name, msg.id))
                .WillOnce(Increase(&n_acked));
        }

        auto worker = get_worker(queue_name,
                [&msgs] (const Message& recv) {
                    EXPECT_EQ(msgs.front(), recv);
                    msgs.pop_front();
                    return true;
                });
        std::thread t(&BaseWorker::run, worker);

        while (n_acked != n_msgs);
        worker->stop();
        t.join();
    }

    TEST_F(WorkerTest, ProcessFailed)
    {
        auto msg = create_msg("message");
        InSequence s;
        EXPECT_CALL(*mock_broker_fetch, getMessage(_, queue_name, msg.id - 1))
            .WillOnce(SetArgReferee<0>(msg));
        EXPECT_CALL(*mock_broker_process, fail(queue_name, msg.id))
            .Times(Exactly(1));

        std::atomic_bool started(false);
        auto worker = get_worker(queue_name,
                [&msg, &started] (const Message& recv) {
                    started = true;
                    EXPECT_EQ(msg, recv);
                    return false;
                });
        std::thread t(&BaseWorker::run, worker);
        while (!started);

        worker->stop();
        t.join();
    }

    TEST_F(WorkerTest, FlowControl)
    {
        int buf_lwm = get_worker_buf_lwm();
        std::atomic_int n_acked(0);
        std::atomic_int n_delivered(0);
        Sequence deliver_seq;
        // 1 in progress, buf_lwm + 1 in queue, 1 extra
        for (int i = 0; i < buf_lwm + 3; ++i) {
            auto msg = create_msg("message" + std::to_string(i));
            InSequence s;
            EXPECT_CALL(*mock_broker_fetch, getMessage(_, queue_name, msg.id - 1))
                .InSequence(deliver_seq)
                .WillOnce(DoAll(SetArgReferee<0>(msg), Increase(&n_delivered)));
            EXPECT_CALL(*mock_broker_process, ack(queue_name, msg.id))
                .WillOnce(Increase(&n_acked));
        }

        std::atomic_bool allowed_to_proceed(false);
        auto worker = get_worker(queue_name,
                [&] (const Message& recv) {
                    while (!allowed_to_proceed);
                    return true;
                });
        std::thread t(&BaseWorker::run, worker);

        while (n_delivered != buf_lwm + 2);
        for (int i = 0; i < 1000; ++i) {  // ensure the last msg is not retrieved
            EXPECT_EQ(buf_lwm + 2, n_delivered);
        }
        allowed_to_proceed = true;
        while (n_acked != buf_lwm + 3);

        worker->stop();
        t.join();
    }

    TEST_F(WorkerTest, EmitMessages)
    {
        std::string ds_queue = "downstream";
        int n_msgs = 10;
        std::atomic_int n_acked(0);
        std::list<std::tuple<Message, Dependency, Message>> msgs;
        Sequence deliver_seq;
        id_t last_us_id = -1;
        for (int i = 0; i < n_msgs; ++i) {
            auto us_msg = create_msg("upstream" + std::to_string(i), std::to_string(i));
            auto ds_msg = create_msg("downstream" + std::to_string(i));
            Dependency dep;
            dep.key = std::to_string(i - 1);
            dep.n = i % 2 ? 3 : 1;
            msgs.emplace_back(us_msg, dep, ds_msg);

            InSequence s;
            EXPECT_CALL(*mock_broker_fetch, getMessage(_, queue_name, last_us_id))
                .InSequence(deliver_seq)
                .WillOnce(SetArgReferee<0>(us_msg));

            if (i % 2) {  // i is odd, should emit 1 msg
                EXPECT_CALL(*mock_broker_process,
                        addMessage(ds_queue, ds_msg, ElementsAre(dep)))
                    .WillOnce(Return(us_msg.id + 10000));
            } else {  // i is even, should emit 3 msgs
                EXPECT_CALL(*mock_broker_process,
                        addMessageGroup(_, ds_queue,
                            ElementsAre(ds_msg, ds_msg, ds_msg),
                            ElementsAre(dep)))
                    .WillOnce(SetArgReferee<0>(
                                std::vector<id_t>({i + 100, i + 101, i + 102})));
            }

            EXPECT_CALL(*mock_broker_process, ack(queue_name, us_msg.id))
                .WillOnce(Increase(&n_acked));

            last_us_id = us_msg.id;
        }

        auto worker = get_worker(queue_name,
                [&] (const Message& recv, TestingWorker* self) {
                    Message us_msg, ds_msg;
                    Dependency dep;
                    std::tie(us_msg, dep, ds_msg) = msgs.front();
                    msgs.pop_front();

                    EXPECT_EQ(us_msg, recv);

                    int i = std::stoi(recv.resolve_dep);
                    if (i % 2) {
                        EXPECT_EQ(us_msg.id + 10000,
                                self->emit(ds_queue, ds_msg, {dep}));
                    } else {
                        EXPECT_THAT(
                                self->emit(ds_queue, {ds_msg, ds_msg, ds_msg}, {dep}),
                                ElementsAre(i + 100, i + 101, i + 102));
                    }

                    return true;
                });
        std::thread t(&BaseWorker::run, worker);

        while (n_acked != n_msgs);
        worker->stop();
        t.join();
    }
}
