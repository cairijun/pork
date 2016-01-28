#include <string>
#include <tuple>
#include <vector>
#include <memory>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/smart_ptr.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "common.h"
#include "flow_control_queue.h"
#include "proto_types.h"
#include "worker.h"

namespace tft = apache::thrift;

namespace pork {

    BaseWorker::BaseWorker(const std::vector<std::string>& zk_hosts,
            const std::string& queue_name):
        running(false),
        zk_handle(get_zk_handle(zk_hosts)),
        msg_buffer(buf_low_water_mark, buf_high_water_mark),
        queue_name(queue_name)
    {
        std::string host;
        uint16_t port;
        get_broker_address(host, port);
        init_broker_client(host, port, true);
        init_broker_client(host, port, false);
    }

    BaseWorker::~BaseWorker()
    {
        if (broker_fetch_transport) {
            broker_fetch_transport->close();
        }
        if (broker_process_transport) {
            broker_process_transport->close();
        }
        if (zk_handle) {
            zookeeper_close(zk_handle);  // TODO: errcode check
        }
    }

    zhandle_t* BaseWorker::get_zk_handle(const std::vector<std::string>& zk_hosts)
    {
        std::string hosts_str = boost::join(zk_hosts, ",");
        return zookeeper_init(
                hosts_str.c_str(), nullptr, zk_recv_timeout, nullptr, nullptr, 0);
    }

    void BaseWorker::init_broker_client(const std::string& host, uint16_t port, bool fetch)
    {
        boost::shared_ptr<tft::transport::TTransport> socket(
                new tft::transport::TSocket(host, port));
        boost::shared_ptr<tft::transport::TTransport> transport(
                new tft::transport::TBufferedTransport(socket));
        boost::shared_ptr<tft::protocol::TProtocol> protocol(
                new tft::protocol::TBinaryProtocol(transport));
        if (fetch) {
            broker_fetch.reset(new BrokerClient(protocol));
            broker_fetch_transport = transport;
        } else {
            broker_process.reset(new BrokerClient(protocol));
            broker_process_transport = transport;
        }
        transport->open();
    }

    void BaseWorker::get_broker_address(std::string& host, uint16_t& port) const
    {
        char buf[256];
        int buf_size = sizeof(buf);
        zoo_get(zk_handle, ZNODE_BROKER_ADDR,
                0, buf, &buf_size, nullptr);  // TODO: errcode check
        host.assign(buf, buf_size);
        size_t semicolon_pos = host.rfind(':');
        port = boost::lexical_cast<uint16_t>(host.substr(semicolon_pos + 1));
        host.erase(semicolon_pos);
    }

    void BaseWorker::run()
    {
        if (running) {
            return;
        }
        running = true;
        std::thread processing_thread(&BaseWorker::process, this);
        while (running) {
            msg_buffer.wait_till_low();
            Message new_msg;
            try {
                broker_fetch->getMessage(new_msg, queue_name, last_msg_id);
            } catch (const Timeout&) {
                continue;
            }
            msg_buffer.put(new_msg);
            last_msg_id = new_msg.id;
        }
        processing_thread.join();
    }

    void BaseWorker::stop()
    {
        running = false;
    }

    void BaseWorker::process()
    {
        while (running) {
            try {
                Message msg = msg_buffer.pop(1000);
                if (process_message(msg)) {
                    broker_process->ack(queue_name, msg.id);
                } else {
                    broker_process->fail(queue_name, msg.id);
                }
            } catch (const decltype(msg_buffer)::Timeout&) {
                // do nothing
            }
        }
    }

    id_t BaseWorker::emit(
            const std::string &queue_name,
            const Message &msg,
            const std::vector<Dependency> &deps) const
    {
        return broker_process->addMessage(queue_name, msg, deps);
    }

    std::vector<id_t> BaseWorker::emit(
            const std::string &queue_name,
            const std::vector<Message> &msgs,
            const std::vector<Dependency> &deps) const
    {
        std::vector<id_t> new_msg_ids;
        broker_process->addMessageGroup(new_msg_ids, queue_name, msgs, deps);
        return new_msg_ids;
    }

} /* pork  */
