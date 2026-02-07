cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)

message(STATUS "Configuring ${PROJECT_NAME}")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(platformer
  src/main.cpp
  src/TileMap.cpp
  src/LevelLoader.cpp
  src/TextRenderer.cpp
  src/LevelSelect.cpp
)
message(STATUS "Added target: platformer")

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_mixer QUIET)
find_package(nlohmann_json REQUIRED)
message(STATUS "Found SDL2: ${SDL2_VERSION}")
message(STATUS "Found SDL2_image: ${SDL2_image_VERSION}")
message(STATUS "Found nlohmann_json: ${nlohmann_json_VERSION}")

target_link_libraries(platformer
  SDL2::SDL2
  SDL2_image::SDL2_image
  SDL2_ttf::SDL2_ttf
  nlohmann_json::nlohmann_json
)
if (SDL2_mixer_FOUND)
  target_link_libraries(platformer SDL2_mixer::SDL2_mixer)
  message(STATUS "Linked platformer: SDL2_mixer::SDL2_mixer")
else()
  message(WARNING "SDL2_mixer not found: music playback disabled")
endif()
message(STATUS "Linked platformer: SDL2::SDL2, nlohmann_json::nlohmann_json")

add_executable(sheet_config
  src/SheetConfigTool.cpp
)
message(STATUS "Added target: sheet_config")

target_link_libraries(sheet_config
  SDL2::SDL2
  SDL2_image::SDL2_image
)
message(STATUS "Linked sheet_config: SDL2::SDL2, SDL2_image::SDL2_image")
