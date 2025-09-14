cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0002 NEW)

project(SMSODestruction VERSION 0.0.1 LANGUAGES CXX)

set(BUILD_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(BUILD_BIN_REACTIONS   "${BUILD_BIN_DIR}/reactions")
set(BUILD_BIN_DRAINS "${BUILD_BIN_DIR}/drains")

# Destino para cópia pós-build
unset(OUTPUT_FOLDER)
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
  set(OUTPUT_FOLDER_REACTIONS   "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}_Reactions")
  set(OUTPUT_FOLDER_DRAINS "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}_Drains")

endif()

if(DEFINED OUTPUT_FOLDER_REACTIONS)
  set(DLL_FOLDER_REACTIONS   "${OUTPUT_FOLDER_REACTIONS}/SKSE/Plugins")
endif()
if(DEFINED OUTPUT_FOLDER_DRAINS)
  set(DLL_FOLDER_DRAINS "${OUTPUT_FOLDER_DRAINS}/SKSE/Plugins")
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

# ----- Elemental reactions

add_commonlibsse_plugin(${PROJECT_NAME}
  SOURCES
    src/PCH.cpp
    src/plugin.ElementalReactions.cpp
    src/elemental_reactions/ElementalStates.cpp
    src/common/PluginSerialization.cpp
    src/elemental_reactions/ElementalGauges.cpp
    src/elemental_reactions/ElementalGaugesHook.cpp
    src/elemental_reactions/ElementalEffects.cpp
    src/hud/HUDTick.cpp
    src/hud/InjectHUD.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${truehud_SOURCE_DIR}/src)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_precompile_headers(
  ${PROJECT_NAME} PRIVATE 
  src/PCH.h
  src/elemental_reactions/ElementalStates.h
  src/common/PluginSerialization.h
  src/common/Helpers.h
  src/elemental_reactions/ElementalGauges.h
  src/elemental_reactions/ElementalGaugesHook.h
  src/elemental_reactions/ElementalEffects.h
  src/hud/HUDTick.h
  src/hud/InjectHUD.h
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  OUTPUT_NAME "SMSODestruction_reactions"
  RUNTIME_OUTPUT_DIRECTORY "${BUILD_BIN_MAIN}/$<CONFIG>"
  ARCHIVE_OUTPUT_DIRECTORY "${BUILD_BIN_MAIN}/$<CONFIG>"
)

# ------- Remove mana and stamina dmg

add_commonlibsse_plugin(${PROJECT_NAME}_Drains
  SOURCES
    src/PCH.cpp          
    src/remove_drains/RemoveDrains.cpp
    src/Plugin.RemoveDrains.cpp
)

target_compile_features(${PROJECT_NAME}_Drains PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME}_Drains PRIVATE
  src/PCH.h
  src/remove_drains/RemoveDrains.h
)

set_target_properties(${PROJECT_NAME}_Drains PROPERTIES
  OUTPUT_NAME "SMSODestruction_drains"
  RUNTIME_OUTPUT_DIRECTORY "${BUILD_BIN_DRAINS}/$<CONFIG>"
  ARCHIVE_OUTPUT_DIRECTORY "${BUILD_BIN_DRAINS}/$<CONFIG>"
)

if(DEFINED DLL_FOLDER_REACTIONS)
  message(STATUS "Main plugin output folder:   ${DLL_FOLDER_REACTIONS}")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER_REACTIONS}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${DLL_FOLDER_REACTIONS}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM
  )
endif()

if(DEFINED DLL_FOLDER_DRAINS)
  message(STATUS "Drains plugin output folder: ${DLL_FOLDER_DRAINS}")
  add_custom_command(TARGET ${PROJECT_NAME}_Drains POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER_DRAINS}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}_Drains>"
            "${DLL_FOLDER_DRAINS}/$<TARGET_FILE_NAME:${PROJECT_NAME}_Drains>"
    VERBATIM
  )
endif()
