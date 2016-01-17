#ifndef FLOW_CONTROL_QUEUE_H_SJ08BHB6
#define FLOW_CONTROL_QUEUE_H_SJ08BHB6

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

namespace pork {

    template<typename T>
    class FlowControlQueue {
        public:
            FlowControlQueue(size_t low_water_mark, size_t high_water_mark);
            FlowControlQueue(const FlowControlQueue&) = delete;

            void put(const T& data);
            T pop();

            std::unique_lock<std::mutex> wait_till_high(bool hold = false);
            std::unique_lock<std::mutex> wait_till_low(bool hold = false);

            size_t size() const { return _q.size(); }
            bool empty() const { return _q.empty(); }
            bool high() const { return size() >= _high_water_mark; }
            bool low() const { return size() <= _low_water_mark; }

        private:
            size_t _low_water_mark;
            size_t _high_water_mark;

            std::queue<T> _q;

            std::mutex _mtx;
            std::condition_variable _cv_high;
            std::condition_variable _cv_low;
            std::condition_variable _cv_not_empty;
    };

    template<typename T>
    FlowControlQueue<T>::FlowControlQueue(size_t low_water_mark, size_t high_water_mark):
        _low_water_mark(low_water_mark),
        _high_water_mark(high_water_mark)
    {
        if (low_water_mark >= high_water_mark) {
            throw std::runtime_error(
                    "low_water_mark must be greater than high_water_mark");
        }
    }

    template<typename T>
    void FlowControlQueue<T>::put(const T& data)
    {
        std::unique_lock<std::mutex> _lock(_mtx);
        _q.push(data);
        _cv_not_empty.notify_all();
        if (high()) {
            _cv_high.notify_all();
        }
    }

    template<typename T>
    T FlowControlQueue<T>::pop()
    {
        std::unique_lock<std::mutex> _lock(_mtx);
        while (empty()) {
            _cv_not_empty.wait(_lock);
        }
        T data = _q.front();
        _q.pop();
        if (low()) {
            _cv_low.notify_all();
        }
        return data;
    }

    template<typename T>
    std::unique_lock<std::mutex> FlowControlQueue<T>::wait_till_high(bool hold)
    {
        std::unique_lock<std::mutex> _lock(_mtx);
        while (!high()) {
            _cv_high.wait(_lock);
        }
        return hold ? std::move(_lock) : std::unique_lock<std::mutex>();
    }

    template<typename T>
    std::unique_lock<std::mutex> FlowControlQueue<T>::wait_till_low(bool hold)
    {
        std::unique_lock<std::mutex> _lock(_mtx);
        while (!low()) {
            _cv_low.wait(_lock);
        }
        return hold ? std::move(_lock) : std::unique_lock<std::mutex>();
    }

} /* pork  */

#endif /* end of include guard: FLOW_CONTROL_QUEUE_H_SJ08BHB6 */
