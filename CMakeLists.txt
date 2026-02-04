cmake_minimum_required(VERSION 3.16)
project(platformer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(platformer
  src/main.cpp
  src/TileMap.cpp
  src/LevelLoader.cpp
)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(nlohmann_json REQUIRED)

target_link_libraries(platformer
  SDL2::SDL2
  nlohmann_json::nlohmann_json
)

add_executable(sheet_config
  src/SheetConfigTool.cpp
)

target_link_libraries(sheet_config
  SDL2::SDL2
  SDL2_image::SDL2_image
)
