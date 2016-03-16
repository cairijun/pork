#include <algorithm>
#include <set>

#include <boost/lexical_cast.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_factories.hpp>
#include <zookeeper/zookeeper.h>

#include "broker/broker_internal.h"
#include "common.h"

namespace pork {

    namespace internal {
        void node_list_changed(
                zhandle_t* zh, int ev_type, int state, const char* path, void* ctx)
        {
            BrokerInternalHandler* handler = static_cast<BrokerInternalHandler*>(ctx);
            String_vector nodes;
            int ret = zoo_wget_children(zh, ZNODE_BROKER_NODES,
                    &internal::node_list_changed, handler, &nodes);
            if (ret != ZOK) {
                throw std::runtime_error(zerror(ret));
            }
            handler->update_node_list(nodes);
        }
    }

    BrokerInternalHandler::BrokerInternalHandler(
            const std::shared_ptr<zhandle_t>& zk_handle,
            const std::string& report_addr):
        BrokerHandler(get_id_block(zk_handle.get())),
        zk_handle(zk_handle), my_addr(report_addr)
    {
        // create a zk node with addr
        char zk_node_path_buf[256];
        int ret = zoo_create(zk_handle.get(), ZNODE_BROKER_NODES_PREFIX,
                report_addr.c_str(), report_addr.size(),
                &ZOO_READ_ACL_UNSAFE, ZOO_SEQUENCE | ZOO_EPHEMERAL,
                zk_node_path_buf, 256);
        if (ret != ZOK) {
            throw std::runtime_error(zerror(ret));
        }
        id_t node_id = boost::lexical_cast<id_t>(
                zk_node_path_buf + strlen(ZNODE_BROKER_NODES_PREFIX));
        this->node_id = node_id;

        // watch node list
        String_vector nodes;
        ret = zoo_wget_children(zk_handle.get(), ZNODE_BROKER_NODES,
                &internal::node_list_changed, this, &nodes);
        if (ret != ZOK) {
            throw std::runtime_error(zerror(ret));
        }
        update_node_list(nodes);
    }

    id_t BrokerInternalHandler::get_id_block(zhandle_t* zk_handle)
    {
        char zk_node_path_buf[256];
        int ret = zoo_create(zk_handle, ZNODE_ID_BLOCK_PREFIX, NULL, -1,
                &ZOO_READ_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE,
                zk_node_path_buf, 256);
        if (ret != ZOK) {
            throw std::runtime_error(zerror(ret));
        }
        id_t block_id = boost::lexical_cast<id_t>(
                zk_node_path_buf + strlen(ZNODE_ID_BLOCK_PREFIX));
        return 1 + (block_id << (sizeof(block_id) * 4));  // block id as the upper half
    }

    void BrokerInternalHandler::update_node_list(const String_vector& new_node_list)
    {
        std::set<int> new_nodes;
        std::vector<int> new_followers;
        for (int i = 0; i < new_node_list.count; ++i) {
            int node;
            try {
                node = boost::lexical_cast<int>(new_node_list.data[i]);
            } catch (boost::bad_lexical_cast) {
                // ignore invalid node
                continue;
            }

            new_nodes.insert(node);
            if (all_nodes.find(node) == all_nodes.end()) {
                new_followers.push_back(node);
            }
        }

        all_nodes.swap(new_nodes);
        // TODO: send snapshot to new followers

        int leader = *std::min_element(all_nodes.cbegin(), all_nodes.cend());
        if (!is_leader && leader == this->node_id) {
            // I'm the leader now
            register_as_leader();
        }
    }

    void BrokerInternalHandler::register_as_leader()
    {
        // TODO: wait till sync complete
        PORK_LOCK(queues_mtx);
        for (auto& q : queues) {
            q.second->start_serving();
        }

        is_leader = true;
        // notify others
        int ret = zoo_create(zk_handle.get(), ZNODE_BROKER_LEADER,
                my_addr.c_str(), my_addr.size(),
                &ZOO_READ_ACL_UNSAFE, ZOO_EPHEMERAL, nullptr, 0);
        if (ret != ZOK) {
            throw std::runtime_error(zerror(ret));
        }
    }

    void BrokerInternalHandler::syncSnapshot(const SnapshotSdto& snapshot)
    {
        PORK_LOCK(queues_mtx);
        queues.clear();
        for (auto& sq : snapshot.queues) {
            queues[sq.first] = std::make_shared<MessageQueue>(sq.second);
        }
    }

    void BrokerInternalHandler::syncSetMessageState(
            const std::string& queue_name,
            const id_t msg_id,
            const MessageState::type state)
    {
        ensure_queue(queue_name)->set_msg_state(msg_id, state);
    }

    void BrokerInternalHandler::syncAddMessages(
            const std::string& queue_name,
            const std::vector<Message>& msgs,
            const std::vector<Dependency>& deps)
    {
        auto q = ensure_queue(queue_name);
        for (auto& m : msgs) {
            q->push_message(std::make_shared<Message>(m), deps);
        }
    }
}
