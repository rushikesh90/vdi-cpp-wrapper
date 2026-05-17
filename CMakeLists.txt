cmake_minimum_required(VERSION 3.16)
project(vdi_wrapper VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Common source list ──────────────────────────────────────────────────
set(VDI_SOURCES
    src/main.cpp
    src/vdi_client.cpp
    src/file_sink.cpp
    src/metrics.cpp
    src/buffer_pool.cpp
    src/sqlvdi_guids.cpp
    src/error_utils.cpp
    src/fault_injector.cpp
)

# ── No-Logging build (OFF) ──────────────────────────────────────────────
add_executable(vdi_wrapper_no_logging EXCLUDE_FROM_ALL ${VDI_SOURCES})
target_include_directories(vdi_wrapper_no_logging PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(vdi_wrapper_no_logging PRIVATE cxx_std_17)
target_compile_definitions(vdi_wrapper_no_logging PRIVATE LOGGING_LEVEL=0)
if(MSVC)
    target_compile_options(vdi_wrapper_no_logging PRIVATE /W4 /permissive-)
else()
    target_compile_options(vdi_wrapper_no_logging PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(vdi_wrapper_no_logging PRIVATE ole32 uuid)

# ── Info build (INFO, default) ──────────────────────────────────────────
add_executable(vdi_wrapper_info EXCLUDE_FROM_ALL ${VDI_SOURCES})
target_include_directories(vdi_wrapper_info PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(vdi_wrapper_info PRIVATE cxx_std_17)
target_compile_definitions(vdi_wrapper_info PRIVATE LOGGING_LEVEL=1)
if(MSVC)
    target_compile_options(vdi_wrapper_info PRIVATE /W4 /permissive-)
else()
    target_compile_options(vdi_wrapper_info PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(vdi_wrapper_info PRIVATE ole32 uuid)

# ── Debug/Trace build (DEBUG) ───────────────────────────────────────────
add_executable(vdi_wrapper_debug EXCLUDE_FROM_ALL ${VDI_SOURCES})
target_include_directories(vdi_wrapper_debug PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(vdi_wrapper_debug PRIVATE cxx_std_17)
target_compile_definitions(vdi_wrapper_debug PRIVATE LOGGING_LEVEL=2)
if(MSVC)
    target_compile_options(vdi_wrapper_debug PRIVATE /W4 /permissive-)
else()
    target_compile_options(vdi_wrapper_debug PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(vdi_wrapper_debug PRIVATE ole32 uuid)

# ── Default target: info logging ────────────────────────────────────────
add_executable(vdi_wrapper ${VDI_SOURCES})
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