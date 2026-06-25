cmake_minimum_required(VERSION 3.15)

project(omnivoice-ggml-cpp
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OMNIVOICE_GGML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ggml" CACHE PATH "Path to vendored ggml")
set(OMNIVOICE_LLAMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/llama.cpp" CACHE PATH "Path to vendored llama.cpp")

if(NOT EXISTS "${OMNIVOICE_GGML_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "vendor/ggml is missing; run git submodule update --init --recursive from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_subdirectory("${OMNIVOICE_GGML_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ggml")

# ── llama.cpp backend (optional, external project to avoid ggml symbol conflicts) ──
option(OMNIVOICE_LLAMA "Build with llama.cpp Qwen3 backend" OFF)
if(OMNIVOICE_LLAMA)
    include(ExternalProject)
    include(GNUInstallDirs)
    set(LLAMA_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/llama-external")
    set(LLAMA_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/llama-install")
    ExternalProject_Add(llama_external
        SOURCE_DIR "${OMNIVOICE_LLAMA_DIR}"
        BINARY_DIR "${LLAMA_BINARY_DIR}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${LLAMA_INSTALL_DIR}
            -DBUILD_SHARED_LIBS=ON
            -DLLAMA_BUILD_TESTS=OFF
            -DLLAMA_BUILD_EXAMPLES=OFF
            -DLLAMA_BUILD_SERVER=OFF
            -DLLAMA_BUILD_TOOLS=OFF
            -DLLAMA_BUILD_APP=OFF
            -DLLAMA_BUILD_COMMON=OFF
            -DGGML_CUDA=OFF
            -DGGML_VULKAN=OFF
            -DGGML_NATIVE=ON
            -DGGML_AVX2=ON
            -DGGML_AVX512=ON
        BUILD_BYPRODUCTS "${LLAMA_INSTALL_DIR}/lib/libllama.so"
    )
    add_library(omnivoice_llama_backend SHARED src/omnivoice_llama.cpp)
    target_include_directories(omnivoice_llama_backend
        PRIVATE
            "${OMNIVOICE_LLAMA_DIR}/include"
            "${OMNIVOICE_GGML_DIR}/include"
    )
    add_library(llama_client SHARED IMPORTED GLOBAL)
    set_target_properties(llama_client PROPERTIES
        IMPORTED_LOCATION "${LLAMA_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/libllama.so"
        INTERFACE_INCLUDE_DIRECTORIES "${OMNIVOICE_LLAMA_DIR}/include")
    target_link_libraries(omnivoice_llama_backend PRIVATE llama_client)
    target_link_options(omnivoice_llama_backend PRIVATE "LINKER:-rpath,\$ORIGIN")
    add_dependencies(omnivoice_llama_backend llama_external)
endif()

add_library(omnivoice_miniaudio STATIC
    vendor/miniaudio/miniaudio.c
)

target_include_directories(omnivoice_miniaudio
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio"
)

target_compile_definitions(omnivoice_miniaudio
    PUBLIC
        MA_NO_DEVICE_IO
        MA_NO_ENGINE
        MA_NO_GENERATION
        MA_NO_NODE_GRAPH
        MA_NO_RESOURCE_MANAGER
)

if(UNIX AND NOT APPLE)
    target_link_libraries(omnivoice_miniaudio PUBLIC m)
endif()

add_library(omnivoice_shine STATIC
    vendor/shine/src/lib/bitstream.c
    vendor/shine/src/lib/huffman.c
    vendor/shine/src/lib/l3bitstream.c
    vendor/shine/src/lib/l3loop.c
    vendor/shine/src/lib/l3mdct.c
    vendor/shine/src/lib/l3subband.c
    vendor/shine/src/lib/layer3.c
    vendor/shine/src/lib/reservoir.c
    vendor/shine/src/lib/tables.c
)

set_target_properties(omnivoice_shine PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(omnivoice_shine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shine/src/lib"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(omnivoice_shine PUBLIC m)
endif()

add_library(omnivoice_ggml_cpp
    src/audio_preprocess.cpp
    src/mp3.cpp
    src/omnivoice.cpp
    src/tokenizer.cpp
    src/wav.cpp
)

target_include_directories(omnivoice_ggml_cpp
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${OMNIVOICE_GGML_DIR}/include"
        "${OMNIVOICE_GGML_DIR}/src"
)

target_link_libraries(omnivoice_ggml_cpp PUBLIC ggml PRIVATE omnivoice_miniaudio omnivoice_shine)

# ── Bridge static library for Go CGo linking ──
add_library(omnivoice_bridge STATIC
    bridge/bridge.cpp
)

target_include_directories(omnivoice_bridge
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/bridge"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(omnivoice_bridge PUBLIC omnivoice_ggml_cpp)

set_property(TARGET omnivoice_bridge PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET omnivoice_ggml_cpp PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET omnivoice_miniaudio PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET omnivoice_shine PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(ggml PRIVATE -march=native -mtune=native)
target_compile_options(omnivoice_ggml_cpp PRIVATE -march=native -mtune=native)
target_compile_options(omnivoice_bridge PRIVATE -march=native -mtune=native)

if (GGML_VULKAN)
    target_compile_definitions(omnivoice_ggml_cpp PRIVATE GGML_USE_VULKAN)
endif()

if(OMNIVOICE_LLAMA)
    target_compile_definitions(omnivoice_ggml_cpp PRIVATE OMNIVOICE_LLAMA)
endif()

# Copy ggml static libs to build root so Go linker can find them
add_custom_command(TARGET omnivoice_bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ggml>" "$<TARGET_FILE:ggml-base>"
        "$<TARGET_FILE:ggml-cpu>"
        "$<TARGET_FILE_DIR:omnivoice_bridge>/"
    COMMAND_EXPAND_LISTS
)
if(TARGET ggml-vulkan)
    add_custom_command(TARGET omnivoice_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ggml-vulkan>"
            "$<TARGET_FILE_DIR:omnivoice_bridge>/"
        COMMAND_EXPAND_LISTS
    )
endif()

add_library(omnivoice_nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS omnivoice_nlohmann_json)
target_include_directories(omnivoice_nlohmann_json
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
)

add_executable(omnivoice-cli src/omnivoice_cli.cpp)
target_link_libraries(omnivoice-cli PRIVATE omnivoice_ggml_cpp)

add_executable(omnivoice-server src/omnivoice_server.cpp)
target_include_directories(omnivoice-server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/httplib")
target_link_libraries(omnivoice-server PRIVATE omnivoice_ggml_cpp nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(omnivoice-server PRIVATE Threads::Threads)

if(OMNIVOICE_LLAMA)
    macro(copy_llama_libs target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LLAMA_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/libllama.so"
                "$<TARGET_FILE_DIR:${target}>/libllama.so"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:omnivoice_llama_backend>"
                "$<TARGET_FILE_DIR:${target}>/libomnivoice_llama_backend.so"
        )
    endmacro()
    copy_llama_libs(omnivoice-cli)
    copy_llama_libs(omnivoice-server)
endif()

install(TARGETS omnivoice_ggml_cpp omnivoice-cli omnivoice-server
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
