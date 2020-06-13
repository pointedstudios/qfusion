project(clienttest LANGUAGES CXX)

cmake_minimum_required(VERSION 2.8.12)

find_package(Qt5Test REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

enable_testing(true)

add_executable(
        clienttest
        "main.cpp"
        "materialsourcetest.cpp"
        "tokensplittertest.cpp"
        "tokenstreamtest.cpp"
        "../../ref/materiallexer.cpp"
        "../../ref/materialsource.cpp")

add_test(NAME clienttest COMMAND clienttest)
set_property(TARGET clienttest PROPERTY CXX_STANDARD 17)
target_link_libraries(clienttest PRIVATE Qt5::Test)
