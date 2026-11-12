/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "internals.h"

#include <cstring>

#include "Tables.h"

namespace MT32Emu {

// UNUSED: const int MIDDLEC = 60;

const Tables &Tables::getInstance() {
	static const Tables instance;
	return instance;
}

// CM32 port: upstream computes these tables here with log(), log10(), sin() and exp2f(). The
// results depend on the C library in the last bit, which is audible to a byte comparison, so the
// port takes them as data. TablesData.inc is not a source file: the build writes it with
// make_tables.py, from upstream's formulas, and puts its directory on the include path.
#include "TablesData.inc"

Tables::Tables() {
	memcpy(levelToAmpSubtraction, CM32_TABLE_levelToAmpSubtraction, sizeof levelToAmpSubtraction);
	memcpy(envLogarithmicTime, CM32_TABLE_envLogarithmicTime, sizeof envLogarithmicTime);
	memcpy(masterVolToAmpSubtraction, CM32_TABLE_masterVolToAmpSubtraction, sizeof masterVolToAmpSubtraction);
	memcpy(pulseWidth100To255, CM32_TABLE_pulseWidth100To255, sizeof pulseWidth100To255);
	memcpy(exp9, CM32_TABLE_exp9, sizeof exp9);
	memcpy(logsin9, CM32_TABLE_logsin9, sizeof logsin9);

	// found from sample analysis
	static const Bit8u resAmpDecayFactorTable[] = {31, 16, 12, 8, 5, 3, 2, 1};
	resAmpDecayFactors = resAmpDecayFactorTable;
}

} // namespace MT32Emu
