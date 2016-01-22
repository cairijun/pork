#include <memory>

#include <boost/chrono/duration.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "common.h"
#include "message_queue.h"
#include "proto_types.h"

namespace pork {

    const boost::chrono::milliseconds MessageQueue::POP_FREE_TIMEOUT(5000);

    bool MessageQueue::pop_free_message(Message& msg)
    {
        boost::unique_lock<boost::mutex> lock(free_msgs_mtx);
        if (free_msgs_not_empty_cv.wait_for(lock, POP_FREE_TIMEOUT,
                    [this]() { return !free_msgs.empty(); })) {
            // free_msgs is not empty, pop the msg and set the state
            auto intern_msg = free_msgs.front();
            intern_msg->state = MessageState::IN_PROGRESS;
            msg = *intern_msg->msg;
            free_msgs.pop();
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
        }

        if (intern_msg->n_deps == 0) {  // check if there are really some deps
            push_free_message(intern_msg);
        }
    }

    void MessageQueue::ack(id_t msg_id)
    {
        PORK_RLOCK(rlock_, all_msgs_mtx);
        auto msg = all_msgs.at(msg_id);
        rlock_all_msgs_mtx.unlock();

        msg->state = MessageState::ACKED;
        if (msg->msg->__isset.resolve_dep) {
            PORK_RLOCK(rlock_, all_deps_mtx);
            auto dep_iter = all_deps.find(msg->msg->resolve_dep);
            if (dep_iter != all_deps.end()) {
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
                    PORK_LOCK(free_msgs_mtx);
                    auto i = dep->dependants.begin();
                    while (i != dep->dependants.end()) {
                        if ((*i)->n_deps == 0) {
                            // push_free_message is not used here to avoid
                            // repeatedly locking-unlocking free_msgs_mtx
                            free_msgs.push(*i);
                            i = dep->dependants.erase(i);
                        } else {
                            ++i;
                        }
                    }
                    // we are not sure if there was only one free msg being added,
                    // just notify all
                    free_msgs_not_empty_cv.notify_all();
                }
            }
        }
    }

    void MessageQueue::fail(id_t msg_id)
    {
        PORK_RLOCK(rlock_, all_msgs_mtx);
        all_msgs.at(msg_id)->state = MessageState::FAILED;
    }

    void MessageQueue::push_free_message(const std::shared_ptr<InternalMessage>& msg)
    {
        PORK_LOCK(free_msgs_mtx);
        free_msgs.push(msg);
        if (free_msgs.size() == 1) {
            // must notify all here. consider multiple threads waiting while
            // multiple threads pushing msgs, and the pushing threads are
            // all scheduled before the (woken) waiting threads
            free_msgs_not_empty_cv.notify_all();
        }
    }

} /* pork */
