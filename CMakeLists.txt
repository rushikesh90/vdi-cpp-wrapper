cmake_minimum_required(VERSION 3.16)
project(vdi_wrapper VERSION 0.1.0 LANGUAGES CXX)

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
    src/vdi_protocol.cpp
    src/file_sink.cpp
    src/metrics.cpp
    src/buffer_pool.cpp
    src/sqlvdi_guids.cpp
    src/error_utils.cpp
    src/fault_injector.cpp
    src/com_runtime.cpp
    src/environment.cpp
)

# ── Common include directories ──────────────────────────────────────────
set(VDI_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
)

# ── Common link libraries ───────────────────────────────────────────────
set(VDI_LIBS
    ole32
    uuid
)

# ── Warning flags ────────────────────────────────────────────────────────
if(MSVC)
    set(VDI_WARNINGS /W4 /permissive- /WX)
else()
    set(VDI_WARNINGS -Wall -Wextra -Wpedantic -Werror)
endif()

# ── Helper: add a target with a specific logging level ───────────────────
function(add_vdi_target TARGET_NAME LOG_LEVEL)
    add_executable(${TARGET_NAME} ${VDI_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE ${VDI_INCLUDES})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${TARGET_NAME} PRIVATE LOGGING_LEVEL=${LOG_LEVEL})
    target_compile_options(${TARGET_NAME} PRIVATE ${VDI_WARNINGS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${VDI_LIBS})
endfunction()

# ── No-Logging build (OFF, LOGGING_LEVEL=0) ──────────────────────────────
add_vdi_target(vdi_wrapper_no_logging 0)

# ── Info build (INFO, LOGGING_LEVEL=1) ───────────────────────────────────
add_vdi_target(vdi_wrapper_info 1)

# ── Debug/Trace build (DEBUG, LOGGING_LEVEL=2) ──────────────────────────
add_vdi_target(vdi_wrapper_debug 2)

# ── Default target: info logging (LOGING_LEVEL=1) ───────────────────────
add_vdi_target(vdi_wrapper 1)

install(TARGETS vdi_wrapper
    RUNTIME DESTINATION bin
)