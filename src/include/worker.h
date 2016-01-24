#ifndef WORKER_H_LGDNAVV3
#define WORKER_H_LGDNAVV3

#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#include <boost/smart_ptr/shared_ptr.hpp>
#include <thrift/transport/TTransport.h>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "flow_control_queue.h"
#include "proto_types.h"

using apache::thrift::transport::TTransport;

namespace pork {

    class BaseWorker {
        public:
            BaseWorker(const std::vector<std::string> &zk_servers,
                    const std::string& queue_name);
            BaseWorker(const BaseWorker &) = delete;
            virtual ~BaseWorker();
            void run();
            virtual bool process_message(const Message &msg) = 0;

        protected:
            id_t emit(
                    const std::string &queue_name,
                    const Message &msg,
                    const std::vector<Dependency> &deps) const;
            std::vector<id_t> emit(
                    const std::string &queue_name,
                    const std::vector<Message> &msgs,
                    const std::vector<Dependency> &deps) const;

        private:
            static zhandle_t* get_zk_handle(const std::vector<std::string>& zk_hosts);
            static void close_zk(zhandle_t *zk_handle);
            void init_broker_client(const std::string& host, uint16_t port, bool fetch);
            void get_broker_address(std::string& host, uint16_t& port) const;
            void process();

            std::unique_ptr<zhandle_t, decltype(close_zk)*> zk_handle;
            std::string queue_name;
            FlowControlQueue<Message> msg_buffer;
            std::unique_ptr<BrokerClient> broker_fetch;
            boost::shared_ptr<TTransport> broker_fetch_transport;
            std::unique_ptr<BrokerClient> broker_process;
            boost::shared_ptr<TTransport> broker_process_transport;
            id_t last_msg_id;

            static const int zk_recv_timeout = 3000;
            static const int buf_low_water_mark = 3;
            static const int buf_high_water_mark = 5;
    };

}

#endif /* end of include guard: WORKER_H_LGDNAVV3 */
