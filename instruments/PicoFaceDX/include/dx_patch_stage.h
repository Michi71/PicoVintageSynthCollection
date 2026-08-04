// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

// include/dx_patch_stage.h
//
// Staging area for a full RDX_Patch: the control side (preset pick, SysEx bulk
// dump) fills it and pushes IPC_CMD_DX_PATCH_APPLY, the producer copies it into
// the engine when it drains the ring. Was a cross-core hand-off before the move
// to the standard runtime model; both ends now sit on core0, so the ring's
// ordering is all that is needed - a patch is never half-applied to a rendered
// block.

#pragma once

#include "dx_engine/RDX_Types.h"

inline RDX_Patch g_dxStagedPatch;

inline RDX_Patch& dx_patch_stage() { return g_dxStagedPatch; }
