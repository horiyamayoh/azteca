cmake_minimum_required(VERSION 3.20)

project(azteca_phase_a_simple_fixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
	phase_a_simple_fixture
	STATIC
	account.cpp
	ambiguous_a.cpp
	ambiguous_b.cpp
	edge_cases.cpp
	hardening.cpp
	order_service.cpp
	service.cpp
	simple_scenarios.cpp
	syntax_matrix.cpp
)
