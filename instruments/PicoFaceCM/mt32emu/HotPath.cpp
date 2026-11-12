// CM32 port: the per-sample path as one translation unit.
//
// Upstream compiles these files separately, so every sample of every partial goes through a
// dozen calls the compiler cannot see into: the ramps, the pitch timer, the wave generators.
// On a Cortex-M33 those calls are a large part of the work. Compiled together, the small
// functions are inlined into Partial::generateOutput(). Nothing in them changes.
//
// The files below are therefore not compiled on their own. (Two of them name a file-level
// pointer differently from upstream so that they can share a translation unit: rampExp9 in
// LA32Ramp.cpp, tvfEnvLogarithmicTime in TVF.cpp.)
#include "LA32Ramp.cpp"
#include "LA32WaveGenerator.cpp"
#include "TVA.cpp"
#include "TVF.cpp"
#include "TVP.cpp"
#include "Partial.cpp"
