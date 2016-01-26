#ifndef WORKER_H_LGDNAVV3
#define WORKER_H_LGDNAVV3

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#include <boost/smart_ptr/shared_ptr.hpp>
#include <thrift/transport/TTransport.h>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "common.h"
#include "flow_control_queue.h"
#include "proto_types.h"

using apache::thrift::transport::TTransport;

namespace pork {

    class BaseWorker {
        template<typename T> friend class TestWorker;

        public:
            BaseWorker(const std::vector<std::string> &zk_servers,
                    const std::string& queue_name);
            BaseWorker(const BaseWorker&) = delete;
            virtual ~BaseWorker();
            void run();
            void stop();
            virtual bool process_message(const Message &msg) = 0;

        protected:
            id_t emit(
                    const std::string& queue_name,
                    const Message& msg,
                    const std::vector<Dependency>& deps) const;
            std::vector<id_t> emit(
                    const std::string &queue_name,
                    const std::vector<Message>& msgs,
                    const std::vector<Dependency>& deps) const;

        private:
            static zhandle_t* get_zk_handle(const std::vector<std::string>& zk_hosts);
            void init_broker_client(const std::string& host, uint16_t port, bool fetch);
            void get_broker_address(std::string& host, uint16_t& port) const;
            void process();

            std::atomic_bool running;
            zhandle_t* zk_handle = nullptr;
            std::string queue_name;
            FlowControlQueue<Message> msg_buffer;
            std::shared_ptr<BrokerIf> broker_fetch;
            boost::shared_ptr<TTransport> broker_fetch_transport;
            std::shared_ptr<BrokerIf> broker_process;
            boost::shared_ptr<TTransport> broker_process_transport;
            id_t last_msg_id;

            static const int zk_recv_timeout = 3000;
            static const int buf_low_water_mark = 3;
            static const int buf_high_water_mark = 5;

        public:
            // for testing
            BaseWorker(const std::string& queue_name,
                    const std::shared_ptr<BrokerIf>& broker_fetch,
                    const std::shared_ptr<BrokerIf>& broker_process):
                running(false),
                broker_fetch(broker_fetch),
                broker_process(broker_process),
                msg_buffer(buf_low_water_mark, buf_high_water_mark),
                queue_name(queue_name) {}
    };

}

#endif /* end of include guard: WORKER_H_LGDNAVV3 */
