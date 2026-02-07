cmake_minimum_required(VERSION 3.28.3)
project(CheckASM-ATT ASM-ATT)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/result.cmake"
  "set(CMAKE_ASM-ATT_COMPILER \"${CMAKE_ASM-ATT_COMPILER}\")\n"
  
  )