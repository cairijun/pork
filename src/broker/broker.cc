#include <cstring>
#include <memory>

#include <boost/smart_ptr.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "broker_handler.h"
#include "common.h"

using namespace pork;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;

int main() {
    // for testing
    const char* zk_addr = "localhost:2181";
    int zk_recv_timeout = 3000;
    const char* broker_addr = "localhost:6783";

    std::shared_ptr<zhandle_t> zk_handle(
            zookeeper_init(zk_addr, nullptr, zk_recv_timeout, 0, nullptr, 0),
            [] (zhandle_t* p) { zookeeper_close(p); });
    if (zk_handle == nullptr) {
        // TODO: err handling
    }

    // create required znodes if not exist
    int ret;
    for (auto n : ZNODE_PATHS_TO_CREATE) {
        ret = zoo_create(zk_handle.get(), n, nullptr, -1,
                &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
        if (ret != ZOK && ret != ZNODEEXISTS) {
            // TODO: err handling
        }
    }

    ret = zoo_create(zk_handle.get(), ZNODE_BROKER_ADDR, broker_addr, strlen(broker_addr),
            &ZOO_READ_ACL_UNSAFE, ZOO_EPHEMERAL, nullptr, 0);
    if (ret == ZNODEEXISTS) {
        // TODO: slave broker
    } else if (ret != ZOK) {
        // TODO: err handling
    }

    // thrift uses boost's smart ptrs
    auto handler = boost::make_shared<BrokerHandler>(zk_handle);
    TThreadedServer server(
            boost::make_shared<BrokerProcessor>(handler),
            boost::make_shared<TServerSocket>(6783),
            boost::make_shared<TBufferedTransportFactory>(),
            boost::make_shared<TBinaryProtocolFactory>());
    server.serve();
}
