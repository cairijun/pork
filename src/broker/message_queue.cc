#include <memory>

#include <boost/chrono/duration.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "broker/message_queue.h"
#include "common.h"
#include "proto_types.h"

namespace pork {

    boost::chrono::milliseconds MessageQueue::POP_FREE_TIMEOUT(5000);

    MessageQueue::MessageQueue(const QueueSdto& sdto): MessageQueue()
    {
        for (auto& m : sdto.all_msgs) {
            all_msgs[m.first] = std::make_shared<InternalMessage>(
                    std::make_shared<Message>(m.second.msg),
                    m.second.n_deps, m.second.state);
            if (m.second.n_deps == 0 && m.second.state == MessageState::QUEUING) {
                free_msgs.push_back(all_msgs[m.first]);
            }
        }

        for (auto& d : sdto.all_deps) {
            std::unique_ptr<InternalDependency> dep(
                    new InternalDependency(d.second.n_resolved));
            for (id_t id : d.second.dependant_ids) {
                dep->dependants.push_back(all_msgs[id]);
            }
            all_deps.emplace(d.first, std::move(dep));
        }
    }

    void MessageQueue::set_msg_state(id_t msg_id, MessageState::type state)
    {
        PORK_RLOCK(rlock_, all_msgs_mtx);
        auto msg_iter = all_msgs.find(msg_id);
        if (msg_iter == all_msgs.end()) {  // set_state before add_msg
            PORK_RLOCK_UPGRADE(rlock_all_msgs_mtx);
            all_msgs[msg_id] = std::make_shared<InternalMessage>(nullptr, 0, state);
        } else {
            rlock_all_msgs_mtx.unlock();
            auto msg = msg_iter->second;

            // we don't have per-msg locks. the following CAS loop guarantees the state
            // to be updated only when the new state is in a latter stage than any
            // previous set_state operations.
            MessageState::type orig = msg->state;
            while (orig < state && !msg->state.compare_exchange_weak(orig, state));

            if (orig < state) {  // did being updated
                if (state == MessageState::ACKED) {
                    ack(msg_id, false);
                }
            }
        }
    }

    bool MessageQueue::pop_free_message(Message& msg)
    {
        boost::unique_lock<boost::mutex> lock(free_msgs_mtx);
        if (free_msgs_not_empty_cv.wait_for(lock, POP_FREE_TIMEOUT,
                    [this]() { return !free_msgs.empty(); })) {
            // free_msgs is not empty, pop the msg and set the state
            auto intern_msg = free_msgs.front();
            intern_msg->state = MessageState::IN_PROGRESS;
            msg = *intern_msg->msg;
            free_msgs.pop_front();
            return true;
        } else {  // timeout
            return false;
        }
    }

    void MessageQueue::push_message(
            const std::shared_ptr<Message>& msg,
            const std::vector<Dependency>& deps)
    {
        auto intern_msg = std::make_shared<InternalMessage>(msg);
        {  // add to new_msg
            PORK_LOCK(all_msgs_mtx);
            all_msgs[msg->id] = intern_msg;
        }

        if (deps.empty()) {  // free message
            push_free_message(intern_msg);
            return;
        }

        {  // register dependencies
            // if we are gonna split the following lock into finer grain locks,
            // it must be aware that after the msg is pushed to all_deps, other threads
            // might resolve this dep immediately and might free this msg
            PORK_LOCK(all_deps_mtx);
            for (auto dep : deps) {
                // will create a null unique_ptr if not exists
                auto& intern_dep = all_deps[dep.key];
                if (intern_dep.get() == nullptr) {
                    intern_dep.reset(new InternalDependency());
                }
                int n_needed = dep.n - intern_dep->n_resolved;
                if (n_needed > 0) {
                    intern_msg->n_deps += n_needed;
                    intern_dep->dependants.push_back(intern_msg);
                }
            }

            if (intern_msg->n_deps == 0) {  // check if there are really some deps
                push_free_message(intern_msg);
            }
        }
    }

    void MessageQueue::ack(id_t msg_id, bool update_free_msgs)
    {
        PORK_RLOCK(rlock_, all_msgs_mtx);
        auto msg = all_msgs.at(msg_id);
        rlock_all_msgs_mtx.unlock();

        if (msg->state != MessageState::IN_PROGRESS) {
            return;
        }

        msg->state = MessageState::ACKED;
        if (msg->msg->__isset.resolve_dep) {
            PORK_RLOCK(rlock_, all_deps_mtx);
            auto dep_iter = all_deps.find(msg->msg->resolve_dep);
            if (dep_iter == all_deps.end()) {
                PORK_RLOCK_UPGRADE(rlock_all_deps_mtx);
                all_deps[msg->msg->resolve_dep].reset(new InternalDependency(1));
            } else {
                auto& dep = dep_iter->second;
                ++dep->n_resolved;

                // using two loops to avoid unnecessary lock upgrade
                // or temporary list creation when there's no new free msg
                bool has_free_msg = false;
                for (auto dependant : dep->dependants) {
                    if (--dependant->n_deps == 0) {
                        has_free_msg = true;
                    }
                }

                if (has_free_msg) {
                    PORK_RLOCK_UPGRADE(rlock_all_deps_mtx);
                    if (update_free_msgs) {
                        PORK_LOCK(free_msgs_mtx);
                    }
                    auto i = dep->dependants.begin();
                    while (i != dep->dependants.end()) {
                        if ((*i)->n_deps == 0) {
                            // push_free_message is not used here to avoid
                            // repeatedly locking-unlocking free_msgs_mtx
                            if (update_free_msgs) {
                                free_msgs.push_back(*i);
                            }
                            i = dep->dependants.erase(i);
                        } else {
                            ++i;
                        }
                    }

                    if (update_free_msgs) {
                        // we are not sure if there was only one free msg being added,
                        // just notify all
                        free_msgs_not_empty_cv.notify_all();
                    }
                }
            }
        }
    }

    void MessageQueue::ack(id_t msg_id)
    {
        ack(msg_id, true);
    }

    void MessageQueue::fail(id_t msg_id)
    {
        PORK_RLOCK(rlock_, all_msgs_mtx);
        all_msgs.at(msg_id)->state = MessageState::FAILED;
    }

    void MessageQueue::push_free_message(const std::shared_ptr<InternalMessage>& msg)
    {
        PORK_LOCK(free_msgs_mtx);
        free_msgs.push_back(msg);
        if (free_msgs.size() == 1) {
            // must notify all here. consider multiple threads waiting while
            // multiple threads pushing msgs, and the pushing threads are
            // all scheduled before the (woken) waiting threads
            free_msgs_not_empty_cv.notify_all();
        }
    }

} /* pork */
