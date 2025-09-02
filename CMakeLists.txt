cmake_minimum_required(VERSION 3.21)
project(SMSODestruction VERSION 0.0.1 LANGUAGES CXX)

# Destino para cópia pós-build (opcional)
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
  set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
  set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
  SOURCES
    src/PCH.cpp
    src/Plugin.cpp
    src/RemoveDrains.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h src/ElementalStates.h src/RemoveDrains.h)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "SMSODestruction")

if(DEFINED OUTPUT_FOLDER)
  set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
  message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

  add_custom_command(TARGET "${PROJECT_NAME}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM
  )
endif()
