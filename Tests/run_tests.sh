#!/bin/sh
# Builds and runs test_logic.cpp. Deliberately dependency-free -- no CMake,
# no test framework, just a compiler. DriftLogic.h/MidiExport.h are
# pure C++ with no iPlug2 dependency, so there's no reason a quick regression
# check should need the SDK, Xcode, or a build-system round trip.
set -eu

cd "$(dirname "$0")"
CXX="${CXX:-clang++}"

"$CXX" -std=c++17 -Wall -Wextra -O1 -o /tmp/drift_logic_tests test_logic.cpp
/tmp/drift_logic_tests
