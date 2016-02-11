#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "worker.h"
#include "proto_types.h"

using namespace pork;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

class Stats {
    public:
        Stats(): last_time(std::chrono::steady_clock::now()) {
            std::thread t([this] () {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    auto delta_t = std::chrono::steady_clock::now() - last_time;
                    last_time = std::chrono::steady_clock::now();
                    double seconds = std::chrono::duration_cast<std::chrono::microseconds>
                            (delta_t).count() * 1e-6;
                    LOG_INFO << counter / seconds << " msgs/s";
                    counter = 0;
                }
            });
            t.detach();
        }

        void operator()() {
            ++counter;
        }

    private:
        std::atomic_size_t counter;
        std::chrono::steady_clock::time_point last_time;
};

class Stage0: public BaseWorker {
    public:
        Stage0(): BaseWorker({"localhost:2181"}, "q0") {}

        bool process_message(const Message& msg) override {
            return false;
        }

        void produce_msgs(int n, int trunk_size = 0) {
            if (trunk_size == 0) {
                for (int i = 0; i < n; ++i) {
                    Message msg;
                    msg.type = MessageType::NORMAL;
                    msg.payload = "message " + std::to_string(i);
                    emit("q1", msg, {});
                }
            } else {
                std::vector<Message> buf;
                for (int i = 0; i < n; ++i) {
                    if (buf.size() == trunk_size) {
                        emit("q1", buf, {});
                        buf.clear();
                    }
                    Message msg;
                    msg.type = MessageType::NORMAL;
                    msg.payload = "message " + std::to_string(i);
                    buf.push_back(msg);
                }
                if (!buf.empty()) {
                    emit("q1", buf, {});
                }
            }
            LOG_INFO << n << " msgs sent to q1";
        }
};

class Stage1: public BaseWorker {
    public:
        Stage1(Stats& s): BaseWorker({"localhost:2181"}, "q1"), stats(s) {}

        bool process_message(const Message& msg) override {
            Message next_msg;
            next_msg.type = MessageType::NORMAL;
            if (prev_id != -1) {
                next_msg.payload = std::to_string(prev_id);
            }
            next_msg.payload += ";" + std::to_string(msg.id);
            prev_id = msg.id;

            std::vector<Message> next_msgs(4, next_msg);
            emit("q2", next_msgs, {});

            stats();
            return true;
        }

    private:
        pork::id_t prev_id = -1;
        Stats& stats;
};

class Stage2: public BaseWorker {
    public:
        Stage2(Stats& s): BaseWorker({"localhost:2181"}, "q2"), stats(s) {}

        bool process_message(const Message& msg) override {
            auto colon_pos = msg.payload.find(';');
            std::string dep_str = msg.payload.substr(0, colon_pos);
            std::vector<Dependency> deps;
            if (!dep_str.empty()) {
                Dependency dep;
                dep.key = dep_str;
                dep.n = 4;
                deps.push_back(dep);
            }

            Message next_msg;
            next_msg.type = MessageType::NORMAL;
            next_msg.__set_resolve_dep(msg.payload.substr(colon_pos + 1));
            next_msg.payload = next_msg.resolve_dep;

            // process
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            emit("q3", next_msg, deps);

            stats();
            return true;
        }

    private:
        Stats& stats;
};

class Stage3: public BaseWorker {
    public:
        Stage3(Stats& s): BaseWorker({"localhost:2181"}, "q3"), stats(s) {}

        bool process_message(const Message& msg) override {
            pork::id_t curr_id = std::stoull(msg.payload);
            if (curr_id < prev_id) {
                LOG_WARNING << "Msg arrived out-of-order: "
                    << prev_id << " arrived before " << msg.id;
            }
            prev_id = curr_id;
            stats();
            return true;
        }

    private:
        pork::id_t prev_id = 0;
        Stats& stats;
};

int main(int argc, char** argv)
{
    if (argc < 2) {
        LOG_FATAL << "No enough arguments!";
    }

    Stats s;

    std::string stage(argv[1]);
    if (stage == "0") {
        if (argc < 3) {
            LOG_FATAL << "No enough arguments!";
        }
        Stage0().produce_msgs(std::stoi(argv[2]));
    } else if (stage == "1") {
        Stage1(s).run();
    } else if (stage == "2") {
        Stage2(s).run();
    } else if (stage == "3") {
        Stage3(s).run();
    }
}
