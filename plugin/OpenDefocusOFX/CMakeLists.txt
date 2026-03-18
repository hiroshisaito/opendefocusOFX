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

# --- Rust target triple (macOS per-architecture build) ---
# Pass -DCMAKE_OSX_ARCHITECTURES=x86_64 or arm64 to select architecture.
# When not specified, builds for the host architecture (same as before).
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(RUST_TARGET "aarch64-apple-darwin")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(RUST_TARGET "x86_64-apple-darwin")
    else()
        set(RUST_TARGET "")
    endif()
endif()

if(RUST_TARGET)
    set(CARGO_TARGET_FLAG "--target" "${RUST_TARGET}")
else()
    set(CARGO_TARGET_FLAG "")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_BUILD_FLAG "--release")
    if(RUST_TARGET)
        set(RUST_LIB_DIR "${RUST_TARGET_DIR}/${RUST_TARGET}/release")
    else()
        set(RUST_LIB_DIR "${RUST_TARGET_DIR}/release")
    endif()
else()
    set(CARGO_BUILD_FLAG "")
    if(RUST_TARGET)
        set(RUST_LIB_DIR "${RUST_TARGET_DIR}/${RUST_TARGET}/debug")
    else()
        set(RUST_LIB_DIR "${RUST_TARGET_DIR}/debug")
    endif()
endif()

set(RUST_STATICLIB "${RUST_LIB_DIR}/libopendefocus_ofx_bridge.a")

# Always run cargo build — cargo itself handles incremental compilation,
# so this is a no-op when sources haven't changed.  This avoids the problem
# where CMake's OUTPUT-based dependency tracking doesn't see Rust source
# changes (the .a already exists) and silently skips recompilation.
add_custom_target(rust_bridge ALL
    COMMAND ${CMAKE_COMMAND} -E env
        PATH=$ENV{HOME}/.cargo/bin:$ENV{PATH}
        cargo build ${CARGO_BUILD_FLAG} ${CARGO_TARGET_FLAG}
    WORKING_DIRECTORY "${RUST_BRIDGE_DIR}"
    COMMENT "Building Rust opendefocus-ofx-bridge staticlib"
    VERBATIM)

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
if(APPLE)
    add_library(OpenDefocus MODULE
        src/OpenDefocusOFX.cpp)
else()
    add_library(OpenDefocus SHARED
        src/OpenDefocusOFX.cpp)
endif()

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
if(APPLE)
    # Rust staticlib native deps are not propagated through CMake, so we link
    # the Apple frameworks/runtime that wgpu/Metal and the overlay OpenGL path use.
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(OBJC_LIB objc REQUIRED)

    target_link_libraries(OpenDefocus PRIVATE
        OfxSupport
        ${RUST_STATICLIB}
        ${OPENGL_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${OBJC_LIB})
else()
    target_link_libraries(OpenDefocus PRIVATE
        OfxSupport
        ${RUST_STATICLIB}
        pthread
        dl
        m)
endif()

# --- Bundle installation ---
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(ARCHDIR "MacOS-x86-64")
    else()
        set(ARCHDIR "MacOS")
    endif()
else()
    set(ARCHDIR "Linux-x86-64")
endif()
set(BUNDLE_DIR "${BUNDLE_ROOT}/OpenDefocusOFX.ofx.bundle/Contents/${ARCHDIR}")

add_custom_command(TARGET OpenDefocus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OpenDefocus>" "${BUNDLE_DIR}/OpenDefocusOFX.ofx"
    COMMENT "Copying OpenDefocusOFX.ofx to bundle")

# --- System install target ---
if(APPLE)
    set(SYSTEM_OFX_DIR "/Library/OFX/Plugins")
else()
    set(SYSTEM_OFX_DIR "/usr/OFX/Plugins")
endif()
install(FILES "$<TARGET_FILE:OpenDefocus>"
    DESTINATION "${SYSTEM_OFX_DIR}/OpenDefocusOFX.ofx.bundle/Contents/${ARCHDIR}"
    RENAME "OpenDefocusOFX.ofx")
