cmake_minimum_required(VERSION 3.20)
project(OpenDefocusOFX VERSION 0.1.10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Paths ---
set(OFX_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../upstream/openfx")
set(OFX_HEADER_DIR "${OFX_SDK_ROOT}/include")
set(OFX_SUPPORT_HEADER_DIR "${OFX_SDK_ROOT}/Support/include")
set(OFX_SUPPORT_LIBRARY_DIR "${OFX_SDK_ROOT}/Support/Library")

set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../bundle")

# --- Rust FFI bridge ---
set(RUST_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../rust/opendefocus-ofx-bridge")
set(RUST_TARGET_DIR "${RUST_BRIDGE_DIR}/target")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_BUILD_FLAG "--release")
    set(RUST_LIB_DIR "${RUST_TARGET_DIR}/release")
else()
    set(CARGO_BUILD_FLAG "")
    set(RUST_LIB_DIR "${RUST_TARGET_DIR}/debug")
endif()

set(RUST_STATICLIB "${RUST_LIB_DIR}/libopendefocus_ofx_bridge.a")

add_custom_command(
    OUTPUT "${RUST_STATICLIB}"
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=$ENV{HOME}/.cargo/bin:$ENV{PATH}
        cargo build ${CARGO_BUILD_FLAG}
    WORKING_DIRECTORY "${RUST_BRIDGE_DIR}"
    COMMENT "Building Rust opendefocus-ofx-bridge staticlib"
    VERBATIM)

add_custom_target(rust_bridge DEPENDS "${RUST_STATICLIB}")

# --- OFX Support Library (static) ---
file(GLOB OFX_SUPPORT_SOURCES "${OFX_SUPPORT_LIBRARY_DIR}/*.cpp")

add_library(OfxSupport STATIC ${OFX_SUPPORT_SOURCES})
set_target_properties(OfxSupport PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LINKER_LANGUAGE CXX)
target_include_directories(OfxSupport PUBLIC
    ${OFX_HEADER_DIR}
    ${OFX_SUPPORT_HEADER_DIR})

# --- Plugin shared library ---
add_library(OpenDefocus SHARED
    src/OpenDefocusOFX.cpp)

set_target_properties(OpenDefocus PROPERTIES
    SUFFIX ".ofx"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

target_include_directories(OpenDefocus PRIVATE
    ${OFX_HEADER_DIR}
    ${OFX_SUPPORT_HEADER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${RUST_BRIDGE_DIR}/include)

add_dependencies(OpenDefocus rust_bridge)
target_link_libraries(OpenDefocus PRIVATE
    OfxSupport
    ${RUST_STATICLIB}
    pthread
    dl
    m)

# --- Bundle installation ---
set(ARCHDIR "Linux-x86-64")
set(BUNDLE_DIR "${BUNDLE_ROOT}/OpenDefocusOFX.ofx.bundle/Contents/${ARCHDIR}")

add_custom_command(TARGET OpenDefocus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OpenDefocus>" "${BUNDLE_DIR}/OpenDefocusOFX.ofx"
    COMMENT "Copying OpenDefocusOFX.ofx to bundle")

# --- System install target ---
set(SYSTEM_OFX_DIR "/usr/OFX/Plugins")
install(FILES "$<TARGET_FILE:OpenDefocus>"
    DESTINATION "${SYSTEM_OFX_DIR}/OpenDefocusOFX.ofx.bundle/Contents/${ARCHDIR}"
    RENAME "OpenDefocusOFX.ofx")
