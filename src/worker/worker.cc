#include <string>
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
        zk_handle(get_zk_handle(zk_hosts), close_zk),
        msg_buffer(buf_low_water_mark, buf_high_water_mark),
        queue_name(queue_name),
        last_msg_id(-1)
    {
        std::string host;
        uint16_t port;
        get_broker_address(host, port);
        boost::shared_ptr<tft::transport::TTransport> socket(
                new tft::transport::TSocket(host, port));
        boost::shared_ptr<tft::transport::TTransport> transport(
                new tft::transport::TBufferedTransport(socket),
                [] (tft::transport::TTransport *p) { p->close(); });
        boost::shared_ptr<tft::protocol::TProtocol> protocol(
                new tft::protocol::TBinaryProtocol(transport));
        broker.reset(new BrokerConcurrentClient(protocol));
    }

    zhandle_t* BaseWorker::get_zk_handle(const std::vector<std::string>& zk_hosts)
    {
        std::string hosts_str = boost::join(zk_hosts, ",");
        return zookeeper_init(
                hosts_str.c_str(), nullptr, zk_recv_timeout, nullptr, nullptr, 0);
    }

    void BaseWorker::close_zk(zhandle_t *zk_handle)
    {
        if (zk_handle) {
            zookeeper_close(zk_handle);  // TODO: errcode check
        }
    }

    void BaseWorker::get_broker_address(std::string& host, uint16_t& port) const
    {
        char buf[256];
        int buf_size = sizeof(buf);
        zoo_get(zk_handle.get(), ZNODE_BROKER_ADDR,
                0, buf, &buf_size, nullptr);  // TODO: errcode check
        host.assign(buf, buf_size);
        size_t semicolon_pos = host.rfind(':');
        port = boost::lexical_cast<uint16_t>(host.substr(semicolon_pos + 1));
        host.erase(semicolon_pos);
    }

    void BaseWorker::run()
    {
        std::thread processing_thread(&BaseWorker::process, this);
        while (true) {
            msg_buffer.wait_till_low();
            Message new_msg;
            broker->getMessage(new_msg, queue_name, last_msg_id);
            msg_buffer.put(new_msg);
            last_msg_id = new_msg.id;
        }
    }

    void BaseWorker::process()
    {
        while (true) {
            Message msg = msg_buffer.pop();
            if (process_message(msg)) {
                broker->ack(msg.id);
            } else {
                broker->fail(msg.id);
            }
        }
    }

    id_t BaseWorker::emit(
            const std::string &queue_name,
            const Message &msg,
            const std::vector<id_t> &deps) const
    {
        return broker->addMessage(queue_name, msg, deps);
    }

    id_t BaseWorker::emit(
            const std::string &queue_name,
            const std::vector<Message> &msgs,
            const std::vector<id_t> &deps) const
    {
        return broker->addMessageGroup(queue_name, msgs, deps);
    }

} /* pork  */
