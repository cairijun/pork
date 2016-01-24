add_gtest_target(test_flow_control_queue test_flow_control_queue.cc)
target_link_libraries(test_flow_control_queue Threads::Threads)

add_executable(worker_test worker_test.cc)
target_link_libraries(worker_test ${WORKER_LIB})
