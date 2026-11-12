/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2026 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#ifndef MT32EMU_PARTIALMANAGER_H
#define MT32EMU_PARTIALMANAGER_H

#include <atomic>

#include "globals.h"
#include "internals.h"
#include "Types.h"

namespace MT32Emu {

class Part;
class Partial;
class Poly;
class Synth;

class PartialManager {
private:
	Synth *synth;
	Part **parts;
	Poly **freePolys;
	Partial **partialTable;
	Bit8u numReservedPartialsForPart[9];
	Bit32u firstFreePolyIndex;
	int *inactivePartials; // Holds indices of inactive Partials in the Partial table
	Bit32u inactivePartialCount;

	bool abortFirstReleasingPolyWhereReserveExceeded(int minPart);
	bool abortFirstPolyPreferHeldWhereReserveExceeded(int minPart);
	bool abortFirstPolyPreferReleasingThenHeldWhereReserveExceeded(int minPart);
	bool abortFirstPolyOnPartPreferReleasingThenHeld(int partNum);
	bool freePartialsNewGen(unsigned int needed, int partNum);

	// CM32 port: two-phase output. A job is one partial that produces output in the current run,
	// together with its ring modulation slave if it has one.
	struct OutputJob {
		Bit8u partialIndex;
		bool reverb;
		Bit16u length;            // samples generated before the partial ended
		Bit8u deactivatedCount;   // partials that ended while this job ran, in that order
		Bit8u deactivated[2];
		Bit16u deactivatedAt[2];  // and the sample each of them did not produce any more
		bool done;                // generated ahead of the others, see generateAbortingPoly()
	};
	Bit16s *outputBuffers;        // MAX_SAMPLES_PER_RUN samples per partial
	OutputJob *outputJobs;
	Bit8u *outputJobOfPartial;
	Bit32u outputJobCount;
	Bit32u jobTimes[2][4];        // per lane: time synth / PCM, partial-samples synth / PCM
	void generateJob(OutputJob &job, Bit32u length, int lane);
	std::atomic<Bit32u> nextOutputJob;
	bool deferringDeactivations;

public:
	static PartialManager *getPartialManager(Synth &synth);

	// CM32 port: what the renderer's loop over produceOutput() does, taken apart.
	//   prepareOutput()   decides which partials sound in this run and whether through the reverb
	//   generateOutput()  generates them, each into its own buffer. May be called from two
	//                     executors at the same time; they share the work out between them.
	//   generateAbortingPoly()  only while a poly is being aborted: generates that poly ahead of
	//                     the rest and returns the length of the run - up to and including the
	//                     sample at which its last partial is gone, or the length asked for
	//   commitOutput()    applies the deactivations generateOutput() ran into
	//   mixOutput()       pans and mixes the reverb or the non-reverb partials, in upstream's order
	// Together they produce exactly what the loop produces.
	void prepareOutput();
	Bit32u generateAbortingPoly(Bit32u length);
	void generateOutput(Bit32u length, int lane);
	Bit32u getOutputJobCount() const { return outputJobCount; }
	void takeJobTimes(Bit32u sums[4]);
	void commitOutput(bool inOrderOfTime);
	void mixOutput(bool reverb, IntSample *leftBuf, IntSample *rightBuf) const;
	bool deferDeactivation(const Partial *partial, const Partial *jobOwner);

	PartialManager(Synth *useSynth);
	~PartialManager();
	Partial *allocPartial(int partNum);
	unsigned int getFreePartialCount();
	void getPerPartPartialUsage(unsigned int perPartPartialUsage[9]);
	const Poly *getAbortingPoly();
	bool freePartials(unsigned int needed, int partNum);
	unsigned int setReserve(Bit8u *rset);
	void deactivateAll();
	bool produceOutput(int i, IntSample *leftBuf, IntSample *rightBuf, Bit32u bufferLength);
	bool produceOutput(int i, FloatSample *leftBuf, FloatSample *rightBuf, Bit32u bufferLength);
	bool shouldReverb(int i);
	void clearAlreadyOutputed();
	const Partial *getPartial(unsigned int partialNum) const;
	Poly *assignPolyToPart(Part *part);
	void polyFreed(Poly *poly);
	void partialDeactivated(int partialIndex);
}; // class PartialManager

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PARTIALMANAGER_H
