cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0002 NEW)

project(ElementalReactionsFramework VERSION 0.0.1 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

# ------ TrueHUD

include(FetchContent)

FetchContent_Declare(
  TrueHUD
  GIT_REPOSITORY https://github.com/ersh1/TrueHUD.git
  GIT_TAG master
)

FetchContent_Populate(TrueHUD)

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(ElementalReactionsFramework
  SOURCES
    src/PCH.cpp
    src/plugin.cpp
    src/Utils.cpp
    src/elemental_reactions/ElementalStates.cpp
    src/common/PluginSerialization.cpp
    src/elemental_reactions/ElementalGauges.cpp
    src/elemental_reactions/ElementalGaugesHook.cpp
    src/hud/HUDTick.cpp
    src/hud/InjectHUD.cpp
    src/hud/TrueHUDMenuWatcher.cpp
    src/elemental_reactions/erf_element.cpp
    src/elemental_reactions/erf_reaction.cpp
)

target_include_directories(ElementalReactionsFramework PRIVATE ${truehud_SOURCE_DIR}/src)

target_compile_features(ElementalReactionsFramework PRIVATE cxx_std_23)

target_precompile_headers(ElementalReactionsFramework PRIVATE 
  src/PCH.h
  src/Utils.h
  src/common/PluginSerialization.h
  src/common/Helpers.h
  src/elemental_reactions/ElementalStates.h
  src/elemental_reactions/ElementalGauges.h
  src/elemental_reactions/ElementalGaugesHook.h
  src/hud/HUDTick.h
  src/hud/InjectHUD.h
  src/hud/TrueHUDMenuWatcher.h
  src/elemental_reactions/erf_element.h
  src/elemental_reactions/erf_reaction.h
)

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
