#ifndef COMMON_H_AEFYHNIS
#define COMMON_H_AEFYHNIS

#include <iostream>  // for debug use

namespace pork {
    static const char* ZNODE_PATHS_TO_CREATE[] = {"/pork", "/pork/broker", "/pork/id"};
    static const char* ZNODE_BROKER_ADDR = "/pork/broker/addr";
    static const char* ZNODE_ID_BLOCK_PREFIX = "/pork/id/block";
}

#define _CONCAT_TOKEN(x, y) x##y
#define CONTCAT_TOKEN(x, y) _CONCAT_TOKEN(x, y)

#define PORK_RLOCK(prefix, m) boost::upgrade_lock<boost::upgrade_mutex> prefix ## m(m)

#define PORK_RLOCK2(prefix, m1, m2) \
    boost::upgrade_lock<boost::upgrade_mutex> prefix ## m1( \
            m1, boost::defer_lock); \
    boost::upgrade_lock<boost::upgrade_mutex> prefix ## m2( \
            m2, boost::defer_lock); \
    boost::lock(prefix ## m1, prefix ## m2)

#define PORK_RLOCK_UPGRADE(l) \
    boost::upgrade_to_unique_lock<boost::upgrade_mutex> __wlock_ ## l(l)

#define PORK_LOCK(m) auto __lock_ ## m = boost::make_unique_lock(m)

#endif /* end of include guard: COMMON_H_AEFYHNIS */
