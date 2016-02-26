#ifndef MESSAGE_QUEUE_H_BYHAK68A
#define MESSAGE_QUEUE_H_BYHAK68A

#include <atomic>
#include <map>
#include <memory>
#include <list>
#include <queue>
#include <vector>

#include <boost/chrono/duration.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "proto_types.h"
#include "internal_types.h"

namespace pork {

    struct InternalMessage {
        const std::shared_ptr<Message> msg;
        std::atomic<MessageState::type> state;
        std::atomic_int n_deps;
        InternalMessage(
                const std::shared_ptr<Message>& msg,
                int n_deps = 0,
                MessageState::type state = MessageState::QUEUING):
            msg(msg), state(state), n_deps(n_deps) {}
    };

    struct InternalDependency {
        std::atomic_int n_resolved;
        std::list<std::shared_ptr<InternalMessage>> dependants;
        InternalDependency(int resolved = 0): n_resolved(resolved) {}
    };

    class AbstractMessageQueue {
        public:
            virtual bool pop_free_message(Message& msg) = 0;
            virtual void push_message(
                    const std::shared_ptr<Message>& msg,
                    const std::vector<Dependency>& deps) = 0;
            virtual void ack(id_t msg_id) = 0;
            virtual void fail(id_t msg_id) = 0;

            // for synchronization
            virtual void set_msg_state(id_t msg_id, MessageState::type state) = 0;
    };

    class MessageQueue: public AbstractMessageQueue {
        friend class BrokerMqTest;

        public:
            MessageQueue() {}
            MessageQueue(const MessageQueue&) = delete;
            bool pop_free_message(Message& msg) override;
            void push_message(
                    const std::shared_ptr<Message>& msg,
                    const std::vector<Dependency>& deps) override;
            void ack(id_t msg_id) override;
            void fail(id_t msg_id) override;

            // for synchronization
            explicit MessageQueue(const QueueSdto& sdto);
            void set_msg_state(id_t msg_id, MessageState::type state) override;

        private:
            void push_free_message(const std::shared_ptr<InternalMessage>& msg);
            void ack(id_t msg_id, bool update_free_msgs);

            std::deque<std::shared_ptr<InternalMessage>> free_msgs;
            // change all shared_ptrs other than the following one to weak_ptrs?
            std::map<id_t, std::shared_ptr<InternalMessage>> all_msgs;
            std::map<std::string, std::unique_ptr<InternalDependency>> all_deps;

            boost::mutex free_msgs_mtx;
            boost::condition_variable free_msgs_not_empty_cv;

            boost::upgrade_mutex all_msgs_mtx;
            boost::upgrade_mutex all_deps_mtx;

            static boost::chrono::milliseconds POP_FREE_TIMEOUT;
    };

} /* pork  */

#endif /* end of include guard: MESSAGE_QUEUE_H_BYHAK68A */
