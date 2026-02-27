cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(FarcalEngineV2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FARCAL_SINGLE_EXE "Build as one self-contained executable (requires static Qt)" OFF)
option(FARCAL_EXTREME_SIZE_OPT "Aggressively optimize Release binaries for smaller file size" OFF)
option(FARCAL_PARALLEL_BUILD "Enable multithreaded compilation settings" ON)
option(FARCAL_DISABLE_RTTI "Disable C++ RTTI metadata generation" ON)

if(FARCAL_SINGLE_EXE)
    add_compile_definitions(FARCAL_SINGLE_EXE=1)
endif()

# Required for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(FetchContent)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(FARCAL_LUA_TAG "v5.4.6" CACHE STRING "Lua git tag to fetch via FetchContent")
set(FARCAL_SOL2_TAG "v3.3.0" CACHE STRING "sol2 git tag to fetch via FetchContent")
set(FARCAL_GLM_TAG "1.0.1" CACHE STRING "GLM git tag to fetch via FetchContent")

FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG ${FARCAL_LUA_TAG}
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG ${FARCAL_SOL2_TAG}
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG ${FARCAL_GLM_TAG}
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()

FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()

file(GLOB LUA_CORE_SOURCES "${lua_SOURCE_DIR}/*.c")
list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/lua\\.c$")
list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/luac\\.c$")
list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/onelua\\.c$")
list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/ltests\\.c$")

add_library(farcal_lua STATIC ${LUA_CORE_SOURCES})
target_include_directories(farcal_lua PUBLIC "${lua_SOURCE_DIR}")
if(WIN32)
    target_compile_definitions(farcal_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(farcal_lua PRIVATE m)
endif()
add_library(lua::lua ALIAS farcal_lua)

add_executable(FarcalEngineV2
    src/main.cpp
    src/app/Application.cpp
    src/memory/ProcessMemoryScanner.cpp
    src/luavm/AttachedProcessContext.cpp
    src/luavm/GlmMatrixBindings.cpp
    src/luavm/GlmQuaternionBindings.cpp
    src/luavm/GlmTypeBindings.cpp
    src/luavm/GlmVectorBindings.cpp
    src/luavm/LuaVmBase.cpp
    src/luavm/MemoryReadBindings.cpp
    src/ui/AttachProcessDialog.cpp
    src/ui/InfoWindow.cpp
    src/ui/LoopWriteManagerWindow.cpp
    src/ui/Logger.cpp
    src/ui/LogWindow.cpp
    src/ui/LuaVmOutputWindow.cpp
    src/ui/LuaVmWindow.cpp
    src/ui/MemoryViewerWindow.cpp
    src/ui/RttiWindow.cpp
    src/ui/SettingsWindow.cpp
    src/ui/StringsWindow.cpp
    src/ui/StructureDissectorWindow.cpp
    src/ui/MainWindow.cpp
    include/farcal/app/Application.hpp
    include/farcal/ui/AttachProcessDialog.hpp
    include/farcal/ui/InfoWindow.hpp
    include/farcal/ui/LoopWriteManagerWindow.hpp
    include/farcal/ui/LoopWriteTypes.hpp
    include/farcal/ui/Logger.hpp
    include/farcal/ui/LogWindow.hpp
    include/farcal/ui/LuaVmOutputWindow.hpp
    include/farcal/ui/LuaVmWindow.hpp
    include/farcal/luavm/AttachedProcessContext.hpp
    include/farcal/luavm/LuaBindings.hpp
    include/farcal/luavm/LuaVmBase.hpp
    include/farcal/memory/MemoryReader.hpp
    include/farcal/memory/ProcessMemoryScanner.hpp
    include/farcal/memory/RttiScanner.hpp
    include/farcal/memory/StringScanner.hpp
    include/farcal/ui/MemoryViewerWindow.hpp
    include/farcal/ui/RttiWindow.hpp
    include/farcal/ui/SettingsTypes.hpp
    include/farcal/ui/SettingsWindow.hpp
    include/farcal/ui/StringsWindow.hpp
    include/farcal/ui/StructureDissectorWindow.hpp
    include/farcal/ui/MainWindow.hpp
    src/luavm/GlmBindingSections.hpp
)

target_include_directories(FarcalEngineV2 PRIVATE include "${sol2_SOURCE_DIR}/include" "${glm_SOURCE_DIR}")

target_link_libraries(FarcalEngineV2 PRIVATE Qt6::Widgets lua::lua)

if(FARCAL_DISABLE_RTTI)
    if(MSVC)
        target_compile_options(FarcalEngineV2 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(FarcalEngineV2 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endif()

if(FARCAL_PARALLEL_BUILD)
    if(MSVC)
        target_compile_options(FarcalEngineV2 PRIVATE /MP /bigobj)
    endif()
endif()

if(FARCAL_EXTREME_SIZE_OPT)
    if(MSVC)
        set_property(TARGET FarcalEngineV2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set_property(TARGET FarcalEngineV2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
        target_compile_options(FarcalEngineV2 PRIVATE
            $<$<CONFIG:Release>:/O1 /Os /Gy /Gw /GL>
            $<$<CONFIG:MinSizeRel>:/O1 /Os /Gy /Gw /GL>
        )
        target_link_options(FarcalEngineV2 PRIVATE
            $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG>
            $<$<CONFIG:MinSizeRel>:/OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG>
        )
    elseif(MINGW)
        target_compile_options(FarcalEngineV2 PRIVATE
            $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections>
            $<$<CONFIG:MinSizeRel>:-Os -ffunction-sections -fdata-sections>
        )
        target_link_options(FarcalEngineV2 PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections -s>
            $<$<CONFIG:MinSizeRel>:-Wl,--gc-sections -s>
        )
    endif()
endif()

if(FARCAL_SINGLE_EXE)
    if(MSVC)
        set_property(TARGET FarcalEngineV2 PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_libraries(FarcalEngineV2 PRIVATE
            $<$<CONFIG:Debug>:msvcprtd>
            $<$<NOT:$<CONFIG:Debug>>:msvcprt>
        )
    endif()

    if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET MATCHES ".*-static.*")
        message(FATAL_ERROR
            "FARCAL_SINGLE_EXE=ON requires static Qt libraries.\n"
            "Current VCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'.\n"
            "Install static Qt first:\n"
            "  vcpkg install qtbase:x64-windows-static\n"
            "Then reconfigure with:\n"
            "  -DVCPKG_TARGET_TRIPLET=x64-windows-static"
        )
    endif()

    if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
        target_link_libraries(FarcalEngineV2 PRIVATE Qt6::QWindowsIntegrationPlugin)
    endif()
endif()

if(WIN32 AND NOT FARCAL_SINGLE_EXE)
    set_target_properties(FarcalEngineV2 PROPERTIES WIN32_EXECUTABLE TRUE)

    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(FARCAL_QT_PLUGINS_RELEASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins")
        set(FARCAL_QT_PLUGINS_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins")
        set(FARCAL_DLL_BIN_RELEASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        set(FARCAL_DLL_BIN_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
    else()
        get_filename_component(FARCAL_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        set(FARCAL_QT_PLUGINS_RELEASE "${FARCAL_QT_PREFIX}/plugins")
        set(FARCAL_QT_PLUGINS_DEBUG "${FARCAL_QT_PREFIX}/plugins")
        set(FARCAL_DLL_BIN_RELEASE "${FARCAL_QT_PREFIX}/bin")
        set(FARCAL_DLL_BIN_DEBUG "${FARCAL_QT_PREFIX}/bin")
    endif()

    if(NOT EXISTS "${FARCAL_QT_PLUGINS_DEBUG}/platforms")
        set(FARCAL_QT_PLUGINS_DEBUG "${FARCAL_QT_PLUGINS_RELEASE}")
    endif()
    if(NOT EXISTS "${FARCAL_DLL_BIN_DEBUG}")
        set(FARCAL_DLL_BIN_DEBUG "${FARCAL_DLL_BIN_RELEASE}")
    endif()

    if(EXISTS "${FARCAL_QT_PLUGINS_RELEASE}/platforms")
        add_custom_command(TARGET FarcalEngineV2 POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:FarcalEngineV2>/platforms"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "$<IF:$<CONFIG:Debug>,${FARCAL_QT_PLUGINS_DEBUG}/platforms,${FARCAL_QT_PLUGINS_RELEASE}/platforms>"
                "$<TARGET_FILE_DIR:FarcalEngineV2>/platforms"
            COMMENT "Copying Qt platform plugins for FarcalEngineV2"
            VERBATIM
        )
    else()
        message(WARNING "Qt platform plugin directory was not found; application may fail to start.")
    endif()
endif()