cmake_minimum_required(VERSION 3.20)

project(azteca_phase_a_robustness_fixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
	phase_a_robustness_fixture
	STATIC
	broken.cpp
	target.cpp
)
