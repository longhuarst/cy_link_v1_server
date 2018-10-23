cmake_minimum_required(VERSION 3.10)
project(cy_link_v1_server)

set(CMAKE_CXX_STANDARD 11  )

add_executable(cy_link_v1_server main.cpp)


target_link_libraries(cy_link_v1_server -lpthread -lboost_thread -lboost_system -lboost_regex -lboost_date_time -lboost_filesystem -lboost_log -lboost_log_setup)
