#include <memory>
#include <set>
#include <string>
#include <vector>

#include <zookeeper/zookeeper.h>

#include "BrokerInternal.h"
#include "broker_handler.h"

namespace pork {
    class BrokerInternalHandler;

    namespace internal {
        void node_list_changed(
                zhandle_t* zh, int ev_type, int state, const char* path, void* ctx);
    };

    class BrokerInternalHandler: public BrokerInternalIf, public BrokerHandler {
        friend void internal::node_list_changed(
                zhandle_t* zh, int ev_type, int state, const char* path, void* ctx);

        public:
            using BrokerHandler::BrokerHandler;
            BrokerInternalHandler(
                    const std::shared_ptr<zhandle_t>& zk_handle,
                    const std::string& report_addr);

            void syncSnapshot(const SnapshotSdto& snapshot) override;
            void syncSetMessageState(
                    const std::string& queue_name,
                    const id_t msg_id,
                    const MessageState::type state) override;
            void syncAddMessages(
                    const std::string& queue_name,
                    const std::vector<Message>& msgs,
                    const std::vector<Dependency>& deps) override;

        private:
            std::shared_ptr<zhandle_t> zk_handle;
            std::string my_addr;
            bool is_leader = false;
            int node_id;
            std::set<int> all_nodes;

            void update_node_list(const String_vector& new_node_list);
            void register_as_leader();

            static id_t get_id_block(zhandle_t* zk_handle);
    };
}
