project(qcommontest LANGUAGES CXX)

cmake_minimum_required(VERSION 2.8.12)

find_package(Qt5Test REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

enable_testing(true)

add_executable(
        qcommontest
        main.cpp
        boundsbuildertest.cpp
        stringsplittertest.cpp
        stringviewtest.cpp)

add_test(NAME qcommontest COMMAND qcommontest)
set_property(TARGET qcommontest PROPERTY CXX_STANDARD 17)
target_link_libraries(qcommontest PRIVATE Qt5::Test)
