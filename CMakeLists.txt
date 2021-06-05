cmake_minimum_required(VERSION 3.0.0)
project(server VERSION 0.1.0)

set(CMAKE_C_FLAGS "-g -Wall -lpthread")
add_executable(server webserver-files/server.c webserver-files/request.c webserver-files/segel.c webserver-files/connection.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(server PRIVATE Threads::Threads)