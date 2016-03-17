#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <boost/program_options.hpp>
#include <boost/smart_ptr.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <zookeeper/zookeeper.h>

#include "Broker.h"
#include "broker/broker_internal.h"
#include "common.h"

using namespace pork;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;

namespace po = boost::program_options;

static po::variables_map get_options(int argc, char** argv)
{
    po::options_description desc("Pork broker");
    desc.add_options()
        ("help,h", "print help message")
        ("zookeeper,Z", po::value<std::string>()->default_value("localhost:2181"),
         "address(es) to zookeeper cluster")
        ("zookeeper-timeout", po::value<int>()->default_value(3000),
         "receive timeout of zookeeper")
        ("address,A", po::value<std::string>()->default_value("localhost:6783"),
         "the address and port to report to others")
        ("port,P", po::value<unsigned short>(),
         "the port to bind, default to the port specified in --address")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        exit(EXIT_SUCCESS);
    }

    return vm;
}

int main(int argc, char** argv)
{
    auto options = get_options(argc, argv);
    std::string zk_addr = options["zookeeper"].as<std::string>();
    int zk_recv_timeout = options["zookeeper-timeout"].as<int>();
    std::string broker_addr = options["address"].as<std::string>();
    unsigned short port;
    if (options.count("port")) {
        port = options["port"].as<unsigned short>();
    } else {
        port = std::stoi(broker_addr.substr(broker_addr.find(':') + 1));
    }

    std::shared_ptr<zhandle_t> zk_handle(
            zookeeper_init(zk_addr.c_str(), nullptr, zk_recv_timeout, 0, nullptr, 0),
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

    // thrift uses boost's smart ptrs
    auto handler = boost::make_shared<BrokerInternalHandler>(zk_handle, broker_addr);
    TThreadedServer server(
            boost::make_shared<BrokerInternalProcessor>(handler),
            boost::make_shared<TServerSocket>(port),
            boost::make_shared<TBufferedTransportFactory>(),
            boost::make_shared<TBinaryProtocolFactory>());
    server.serve();
}
