// RDX_State.h
#pragma once
#include "RDX_Types.h"

class RDX_State {
public:
    RDX_State(const RDX_State&) = delete;
    RDX_State& operator=(const RDX_State&) = delete;
    static SynthState& getState() { static SynthState instance; return instance; }
    static bool isInitialized() { return true; }
};
