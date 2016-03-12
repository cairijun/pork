#ifndef COMMON_H_AEFYHNIS
#define COMMON_H_AEFYHNIS

#include <iostream>  // for debug use

#define BOOST_LOG_DYN_LINK 1
#include <boost/log/trivial.hpp>

using namespace boost::log::trivial;

namespace pork {
    static const char* ZNODE_PATHS_TO_CREATE[] = {"/pork", "/pork/broker", "/pork/id"};
    static const char* ZNODE_BROKER_ADDR = "/pork/broker/addr";
    static const char* ZNODE_ID_BLOCK_PREFIX = "/pork/id/block";
}

#define LOG_DEBUG BOOST_LOG_TRIVIAL(debug)
#define LOG_INFO BOOST_LOG_TRIVIAL(info)
#define LOG_WARNING BOOST_LOG_TRIVIAL(warning)
#define LOG_ERROR BOOST_LOG_TRIVIAL(error)
#define LOG_FATAL BOOST_LOG_TRIVIAL(fatal)

#define _CONCAT_TOKEN(x, y) x##y
#define CONTCAT_TOKEN(x, y) _CONCAT_TOKEN(x, y)

#define PORK_RLOCK(prefix, m) boost::shared_lock<boost::upgrade_mutex> prefix ## m(m)

#define PORK_ULOCK(prefix, m) boost::upgrade_lock<boost::upgrade_mutex> prefix ## m(m)

#define PORK_ULOCK_UPGRADE(l) \
    boost::upgrade_to_unique_lock<boost::upgrade_mutex> __wlock_ ## l(l)

#define PORK_LOCK(m) auto __lock_ ## m = boost::make_unique_lock(m)

#endif /* end of include guard: COMMON_H_AEFYHNIS */
