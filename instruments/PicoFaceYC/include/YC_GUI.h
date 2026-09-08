// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#pragma once

#include "picoface/ui.h"
#include "YC_Controller.h"

// Draws the current front panel page through the shared kit: header, then the
// page's one or two values. Paints only - clearing and pushing belong to the
// caller (YC_Ui).
void ycDrawScreen(picoface::ui::Display& d, YC_Controller& controller);
