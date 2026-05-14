cmake_minimum_required(VERSION 3.16)
project(vdi_wrapper VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_executable(vdi_wrapper
    src/main.cpp
    src/vdi_client.cpp
    src/backup_engine.cpp
    src/backup_session.cpp
    src/file_sink.cpp
    src/sink.cpp
    src/sqlvdi_guids.cpp
)

target_include_directories(vdi_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_features(vdi_wrapper PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(vdi_wrapper PRIVATE /W4 /permissive-)
else()
    target_compile_options(vdi_wrapper PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(vdi_wrapper PRIVATE ole32 uuid)

install(TARGETS vdi_wrapper
    RUNTIME DESTINATION bin
)
