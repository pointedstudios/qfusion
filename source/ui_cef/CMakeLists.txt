project(ui_cef)

file(GLOB UI_CEF_HEADERS "*.h")
file(GLOB UI_CEF_SOURCES "*.cpp" "syscalls/*.cpp" "../client/ServerInfo.cpp" "../gameshared/q_math.cpp" "../gameshared/q_shared.cpp")

include_directories(${CEF_INCLUDE_DIR})
link_directories(${CEF_LINK_DIR})

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(wsw_ui_process WIN32 ${UI_CEF_HEADERS} ${UI_CEF_SOURCES})
else()
    add_executable(wsw_ui_process ${UI_CEF_HEADERS} ${UI_CEF_SOURCES})
endif()

target_link_libraries(wsw_ui_process)
target_link_libraries(wsw_ui_process PRIVATE libcef_dll_wrapper)
qf_set_output_dir(wsw_ui_process "")
set_property(TARGET wsw_ui_process PROPERTY CXX_STANDARD 14)