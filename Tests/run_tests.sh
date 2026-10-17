#!/bin/sh
# Runs the Audio Unit identity check, then builds and runs test_logic.cpp.
# The identity check reads config.h and the committed plists. It does not
# build the Audio Unit. The C++ part is deliberately dependency-free -- no
# CMake, no test framework, just a compiler. DriftLogic.h/MidiExport.h are
# pure C++ with no iPlug2 dependency, so there's no reason a quick regression
# check should need the SDK, Xcode, or a build-system round trip.
set -eu

cd "$(dirname "$0")"
CXX="${CXX:-clang++}"

python3 test_plug_identity.py

"$CXX" -std=c++17 -Wall -Wextra -O1 -o /tmp/drift_logic_tests test_logic.cpp
/tmp/drift_logic_tests
