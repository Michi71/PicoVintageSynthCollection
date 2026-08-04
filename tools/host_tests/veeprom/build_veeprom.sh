#!/bin/bash
# Host test for the virtual EEPROM. This one targets the CORE, not an
# instrument: veeprom.cpp moved into core/ with the monorepo merge, and every
# instrument that persists anything sits on top of it.
set -e
cd "$(dirname "$0")"
REPO="$(cd ../../.. && pwd)"
c++ -std=c++17 -O1 -Wall -DVEEPROM_HOST_TEST -I"$REPO/core/include" \
    veeprom_test.cpp "$REPO/core/src/veeprom.cpp" -o veeprom_test
./veeprom_test
