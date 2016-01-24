#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "flow_control_queue.h"

using namespace pork;

TEST(FlowControlQueue, Basic)
{
    FlowControlQueue<int> q(2, 4);

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.low());
    EXPECT_FALSE(q.high());

    q.put(1);
    q.put(2);

    EXPECT_FALSE(q.empty());
    EXPECT_EQ(2, q.size());
    EXPECT_TRUE(q.low());
    EXPECT_FALSE(q.high());

    q.put(3);

    EXPECT_FALSE(q.low());
    EXPECT_FALSE(q.high());

    q.put(4);

    EXPECT_FALSE(q.low());
    EXPECT_TRUE(q.high());

    q.put(5);

    EXPECT_FALSE(q.low());
    EXPECT_TRUE(q.high());
    EXPECT_EQ(5, q.size());

    EXPECT_EQ(1, q.pop());
    EXPECT_EQ(2, q.pop());
    EXPECT_FALSE(q.low());
    EXPECT_FALSE(q.high());

    EXPECT_EQ(3, q.pop());
    EXPECT_TRUE(q.low());
    EXPECT_FALSE(q.high());

    EXPECT_EQ(4, q.pop());
    EXPECT_EQ(5, q.pop());
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(0, q.size());
    EXPECT_TRUE(q.low());
    EXPECT_FALSE(q.high());
}

TEST(FlowControlQueue, InvalidWaterMarks)
{
    EXPECT_THROW(FlowControlQueue<int>(5, 5), std::runtime_error);
    EXPECT_THROW(FlowControlQueue<int>(6, 5), std::runtime_error);
}

TEST(FlowControlQueue, PopEmptyQueue)
{
    int n_threads = 20;
    FlowControlQueue<int> q(2, 5);

    std::atomic_int n_wait(0);
    std::atomic_int pushed;
    std::atomic_int popped;

    auto t = [&] () {
        ++n_wait;
        popped = q.pop();
        EXPECT_EQ(pushed, popped);
        --n_wait;
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < n_threads; ++i) {
        ts.emplace_back(t);
    }

    while (n_wait != n_threads);

    for (int i = 0; i < n_threads; ++i) {
        EXPECT_EQ(n_threads - i, n_wait);
        pushed = i;
        q.put(pushed);
        while (n_wait != n_threads - i - 1);
        EXPECT_EQ(pushed, popped);
    }

    for (auto& t : ts) {
        t.join();
    }
}

void TestWaitWaterMark(bool low)
{
    int n_threads = 20;  // must be even!
    FlowControlQueue<int> q(2, 4);

    q.put(1);
    q.put(2);
    q.put(3);
    ASSERT_FALSE(q.low());
    ASSERT_FALSE(q.high());

    std::atomic_int n_wait(0);
    std::atomic_bool should_exit(false);

    auto t = [&] (bool hold) {
        ++n_wait;
        auto lock = low ? q.wait_till_low(hold) : q.wait_till_high(hold);
        if (hold) {
            for (int i = 0; i < 100; ++i) {
                if (low) {
                    EXPECT_TRUE(q.low());
                } else {
                    EXPECT_TRUE(q.high());
                }
            }
            lock.unlock();
            while (!should_exit);
        } else {
            EXPECT_FALSE(lock) << "A non-hold waiting shouldn't return a locked lock";
        }
        --n_wait;
    };

    std::vector<std::thread> ts;

    for (int i = 0; i < n_threads; ++i) {
        ts.emplace_back(t, i % 2);
    }

    while (n_wait != n_threads);
    if (low) {
        q.pop();
    } else {
        q.put(1);
    }
    while (n_wait != n_threads / 2);  // half threads should be eventually exit
    should_exit = true;

    for (auto& t : ts) {
        t.join();
    }

    EXPECT_EQ(0, n_wait);
}

TEST(FlowControlQueue, WaitTillLow)
{
    TestWaitWaterMark(true);
}

TEST(FlowControlQueue, WaitTillHigh)
{
    TestWaitWaterMark(false);
}
