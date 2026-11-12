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

#include <cstddef>
#include <cstring>

#include "internals.h"

#include "PartialManager.h"
#include "Part.h"
#include "Partial.h"
#include "Poly.h"
#include "Synth.h"

namespace MT32Emu {

CM32_HOT PartialManager *PartialManager::getPartialManager(Synth &synth) {
	return synth.partialManager;
}

CM32_HOT PartialManager::PartialManager(Synth *useSynth) {
	synth = useSynth;
	parts = useSynth->parts;
	inactivePartialCount = synth->getPartialCount();
	partialTable = new Partial *[inactivePartialCount];
	inactivePartials = new int[inactivePartialCount];
	freePolys = new Poly *[synth->getPartialCount()];
	// CM32 port
	outputBuffers = new Bit16s[synth->getPartialCount() * MAX_SAMPLES_PER_RUN];
	outputJobs = new OutputJob[synth->getPartialCount()];
	outputJobOfPartial = new Bit8u[synth->getPartialCount()];
	outputJobCount = 0;
	nextOutputJob = 0;
	deferringDeactivations = false;
	memset(jobTimes, 0, sizeof jobTimes);
	firstFreePolyIndex = 0;
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i] = new Partial(synth, i);
		inactivePartials[i] = inactivePartialCount - i - 1;
		freePolys[i] = new Poly();
	}
}

CM32_HOT PartialManager::~PartialManager(void) {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		delete partialTable[i];
		if (freePolys[i] != NULL) delete freePolys[i];
	}
	delete[] partialTable;
	delete[] inactivePartials;
	delete[] freePolys;
	delete[] outputBuffers;
	delete[] outputJobs;
	delete[] outputJobOfPartial;
}

// CM32 port
CM32_HOT void PartialManager::prepareOutput() {
	outputJobCount = 0;
	for (Bit32u i = 0; i < synth->getPartialCount(); i++) {
		Partial *partial = partialTable[i];
		// Upstream asks each partial in turn, after the ones before it have been rendered. Asking
		// them all beforehand gives the same answers: rendering one partial never changes whether
		// another produces output, except that a master takes its ring modulation slave down with
		// it - and a slave does not produce output of its own in either case.
		if (!partial->canProduceOutput()) continue;
		OutputJob &job = outputJobs[outputJobCount];
		job.partialIndex = Bit8u(i);
		job.reverb = partial->shouldReverb();
		job.length = 0;
		job.deactivatedCount = 0;
		job.done = false;
		outputJobOfPartial[i] = Bit8u(outputJobCount++);
	}
	nextOutputJob = 0;
	deferringDeactivations = true;
}

CM32_HOT void PartialManager::generateJob(OutputJob &job, Bit32u length, int lane) {
	Partial *partial = partialTable[job.partialIndex];
	Bit16s *buffer = outputBuffers + job.partialIndex * MAX_SAMPLES_PER_RUN;
	if (synth->jobClock == NULL) {
		job.length = Bit16u(partial->generateOutput(buffer, length));
		return;
	}
	// Decided before the partial runs: it may be gone afterwards.
	const int kind = partial->isPCM() ? 1 : 0;
	const Bit32u partials = partial->hasRingModulatingSlave() ? 2 : 1;
	const Bit32u start = synth->jobClock();
	job.length = Bit16u(partial->generateOutput(buffer, length));
	jobTimes[lane][kind] += synth->jobClock() - start;
	jobTimes[lane][2 + kind] += partials * job.length;
}

CM32_HOT void PartialManager::generateOutput(Bit32u length, int lane) {
	for (;;) {
		const Bit32u jobIx = nextOutputJob.fetch_add(1, std::memory_order_relaxed);
		if (jobIx >= outputJobCount) break;
		OutputJob &job = outputJobs[jobIx];
		if (job.done) continue;
		generateJob(job, length, lane);
	}
}

void PartialManager::takeJobTimes(Bit32u sums[4]) {
	for (int i = 0; i < 4; i++) {
		sums[i] = jobTimes[0][i] + jobTimes[1][i];
		jobTimes[0][i] = jobTimes[1][i] = 0;
	}
}

CM32_HOT Bit32u PartialManager::generateAbortingPoly(Bit32u length) {
	const Poly *poly = synth->abortingPoly;
	Bit32u ended = 0;
	Bit32u lastEnd = 0;
	bool found = false;
	for (Bit32u jobIx = 0; jobIx < outputJobCount; jobIx++) {
		OutputJob &job = outputJobs[jobIx];
		Partial *partial = partialTable[job.partialIndex];
		// A ring modulation slave belongs to the poly of its master, so the poly is all in these jobs.
		if (partial->poly != poly) continue;
		found = true;
		generateJob(job, length, 0);
		job.done = true;
		for (Bit32u i = 0; i < job.deactivatedCount; i++) {
			ended++;
			if (lastEnd < job.deactivatedAt[i]) lastEnd = job.deactivatedAt[i];
		}
	}
	// Nothing of the poly produces output: upstream's single sample, so that the run makes progress.
	if (!found) return 1;
	// Upstream's run for the sample at which the last partial ends still renders that sample for
	// everything else; the abort is over for the run after it.
	if (ended == poly->getActivePartialCount()) return lastEnd + 1;
	return length;
}

CM32_HOT bool PartialManager::deferDeactivation(const Partial *partial, const Partial *jobOwner) {
	if (!deferringDeactivations) return false;
	// Only the executor that runs the job writes to it.
	OutputJob &job = outputJobs[outputJobOfPartial[jobOwner->partialIndex]];
	job.deactivated[job.deactivatedCount] = Bit8u(partial->partialIndex);
	job.deactivatedAt[job.deactivatedCount++] = Bit16u(jobOwner->sampleNum);
	return true;
}

CM32_HOT void PartialManager::commitOutput(bool inOrderOfTime) {
	deferringDeactivations = false;
	// The order decides which partial a later note is given, so it has to be upstream's.
	// Upstream renders a run partial by partial and so comes across the deactivations job by job.
	// A run that stands for upstream's single-sample runs during an abort has to apply them as
	// those would: sample by sample, and job by job within a sample.
	Bit32u sample = 0;
	for (;;) {
		Bit32u nextSample = 0xFFFFFFFF;
		for (Bit32u jobIx = 0; jobIx < outputJobCount; jobIx++) {
			const OutputJob &job = outputJobs[jobIx];
			for (Bit32u i = 0; i < job.deactivatedCount; i++) {
				if (inOrderOfTime && job.deactivatedAt[i] != sample) {
					if (job.deactivatedAt[i] > sample && job.deactivatedAt[i] < nextSample) nextSample = job.deactivatedAt[i];
					continue;
				}
				Partial *partial = partialTable[job.deactivated[i]];
				partialDeactivated(partial->partialIndex);
				if (partial->poly != NULL) {
					partial->poly->partialDeactivated(partial);
				}
				if (partial->pendingPairUnlink) {
					partial->pendingPairUnlink = false;
					if (partial->pair != NULL) {
						partial->pair->pair = NULL;
					}
				}
			}
		}
		if (!inOrderOfTime || nextSample == 0xFFFFFFFF) break;
		sample = nextSample;
	}
}

CM32_HOT void PartialManager::mixOutput(bool reverb, IntSample *leftBuf, IntSample *rightBuf) const {
	for (Bit32u jobIx = 0; jobIx < outputJobCount; jobIx++) {
		const OutputJob &job = outputJobs[jobIx];
		if (job.reverb != reverb) continue;
		partialTable[job.partialIndex]->mixOutput(outputBuffers + job.partialIndex * MAX_SAMPLES_PER_RUN, job.length, leftBuf, rightBuf);
	}
}

CM32_HOT void PartialManager::clearAlreadyOutputed() {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i]->alreadyOutputed = false;
	}
}

CM32_HOT bool PartialManager::shouldReverb(int i) {
	return partialTable[i]->shouldReverb();
}

CM32_HOT bool PartialManager::produceOutput(int i, IntSample *leftBuf, IntSample *rightBuf, Bit32u bufferLength) {
	return partialTable[i]->produceOutput(leftBuf, rightBuf, bufferLength);
}

CM32_HOT bool PartialManager::produceOutput(int i, FloatSample *leftBuf, FloatSample *rightBuf, Bit32u bufferLength) {
	return partialTable[i]->produceOutput(leftBuf, rightBuf, bufferLength);
}

CM32_HOT void PartialManager::deactivateAll() {
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		partialTable[i]->deactivate();
	}
}

CM32_HOT unsigned int PartialManager::setReserve(Bit8u *rset) {
	unsigned int pr = 0;
	for (int x = 0; x <= 8; x++) {
		numReservedPartialsForPart[x] = rset[x];
		pr += rset[x];
	}
	return pr;
}

CM32_HOT Partial *PartialManager::allocPartial(int partNum) {
	if (inactivePartialCount > 0) {
		Partial *partial = partialTable[inactivePartials[--inactivePartialCount]];
		partial->activate(partNum);
		return partial;
	}
	synth->printDebug("PartialManager Error: No inactive partials to allocate for part %d, current partial state:\n", partNum);
	for (Bit32u i = 0; i < synth->getPartialCount(); i++) {
		const Partial *partial = partialTable[i];
		synth->printDebug("[Partial %d]: activation=%d, owner part=%d\n", i, partial->isActive(), partial->getOwnerPart());
	}
	return NULL;
}

CM32_HOT unsigned int PartialManager::getFreePartialCount() {
	return inactivePartialCount;
}

// This function is solely used to gather data for debug output at the moment.
CM32_HOT void PartialManager::getPerPartPartialUsage(unsigned int perPartPartialUsage[9]) {
	memset(perPartPartialUsage, 0, 9 * sizeof(unsigned int));
	for (unsigned int i = 0; i < synth->getPartialCount(); i++) {
		if (partialTable[i]->isActive()) {
			perPartPartialUsage[partialTable[i]->getOwnerPart()]++;
		}
	}
}

CM32_HOT const Poly *PartialManager::getAbortingPoly() {
	return synth->abortingPoly;
}

// Finds the lowest-priority part that is exceeding its reserved partial allocation and has a poly
// in POLY_Releasing, then kills its first releasing poly.
// Parts with higher priority than minPart are not checked.
// Assumes that getFreePartials() has been called to make numReservedPartialsForPart up-to-date.
CM32_HOT bool PartialManager::abortFirstReleasingPolyWhereReserveExceeded(int minPart) {
	if (minPart == 8) {
		// Rhythm is highest priority
		minPart = -1;
	}
	for (int partNum = 7; partNum >= minPart; partNum--) {
		int usePartNum = partNum == -1 ? 8 : partNum;
		if (parts[usePartNum]->getActivePartialCount() > numReservedPartialsForPart[usePartNum]) {
			// This part has exceeded its reserved partial count.
			// If it has any releasing polys, kill its first one and we're done.
			if (parts[usePartNum]->abortFirstPoly(POLY_Releasing)) {
				return true;
			}
		}
	}
	return false;
}

// Finds the lowest-priority part that is exceeding its reserved partial allocation and has a poly, then kills
// its first poly in POLY_Held - or failing that, its first poly in any state.
// Parts with higher priority than minPart are not checked.
// Assumes that getFreePartials() has been called to make numReservedPartialsForPart up-to-date.
CM32_HOT bool PartialManager::abortFirstPolyPreferHeldWhereReserveExceeded(int minPart) {
	if (minPart == 8) {
		// Rhythm is highest priority
		minPart = -1;
	}
	for (int partNum = 7; partNum >= minPart; partNum--) {
		int usePartNum = partNum == -1 ? 8 : partNum;
		if (parts[usePartNum]->getActivePartialCount() > numReservedPartialsForPart[usePartNum]) {
			// This part has exceeded its reserved partial count.
			// If it has any polys, kill its first (preferably held) one and we're done.
			if (parts[usePartNum]->abortFirstPolyPreferHeld()) {
				return true;
			}
		}
	}
	return false;
}

CM32_HOT bool PartialManager::abortFirstPolyPreferReleasingThenHeldWhereReserveExceeded(int minPartNum) {
	for (int candidatePartNum = 7; candidatePartNum >= minPartNum; candidatePartNum--) {
		if (parts[candidatePartNum]->getActivePartialCount() > numReservedPartialsForPart[candidatePartNum]) {
			return abortFirstPolyOnPartPreferReleasingThenHeld(candidatePartNum);
		}
	}
	return false;
}

CM32_HOT bool PartialManager::abortFirstPolyOnPartPreferReleasingThenHeld(int partNum) {
	if (parts[partNum]->abortFirstPoly(POLY_Releasing)) {
		return true;
	}
	return parts[partNum]->abortFirstPolyPreferHeld();
}

CM32_HOT bool PartialManager::freePartials(unsigned int needed, int partNum) {
	// NOTE: Currently, we don't consider peculiarities of partial allocation when a timbre involves structures with ring modulation.

	if (synth->controlROMFeatures->newGenNoteCancellation) {
		return freePartialsNewGen(needed, partNum);
	}

	while (!synth->isAbortingPoly() && getFreePartialCount() < needed) {
		if (parts[partNum]->getActiveNonReleasingPartialCount() + needed > numReservedPartialsForPart[partNum]) {
			// If priority is given to earlier polys, there's nothing we can do.
			if (synth->getPart(partNum)->getPatchTemp()->patch.assignMode & 1) return false;

			if (needed <= numReservedPartialsForPart[partNum]) {
				// No more partials needed than reserved for this part, only attempt to free partials on the same part.
				abortFirstPolyOnPartPreferReleasingThenHeld(partNum);
				continue;
			}

			// More partials desired than reserved, try to borrow some from parts with lesser priority.
			// NOTE: there's a bug here in the old-gen program, so that the device exhibits undefined behaviour when trying to play
			// a note on the rhythm part beyond the reserve while none of the voice parts has exceeded their reserve.
			// We don't emulate this here, assuming that the intention was to traverse all voice parts, then check the rhythm part.
			if (abortFirstPolyPreferReleasingThenHeldWhereReserveExceeded(partNum < 8 ? partNum : 0)) continue;

			// At this point, old-gen devices try to borrow partials from the rhythm part if it's exceeding the reservation.
			if (parts[8]->getActivePartialCount() > numReservedPartialsForPart[8]
				&& abortFirstPolyOnPartPreferReleasingThenHeld(8)) continue;

			// Alas, this one will be muted.
			return false;
		}

		// OK, we're not going to exceed the reserve. Reclaim our partials from other parts starting from 7 to 0.
		if (abortFirstPolyPreferReleasingThenHeldWhereReserveExceeded(0)) continue;

		// Now try the rhythm part.
		if (parts[8]->getActivePartialCount() > numReservedPartialsForPart[8]
			&& abortFirstPolyOnPartPreferReleasingThenHeld(8)) continue;

		// Lastly, try to silence a poly on this part.
		if (abortFirstPolyOnPartPreferReleasingThenHeld(partNum)) continue;

		// Fair enough, there's no room for it.
		return false;
	}
	return true;
}

CM32_HOT bool PartialManager::freePartialsNewGen(unsigned int needed, int partNum) {
	// CONFIRMED: Barring bugs, this matches the real LAPC-I according to information from Mok.

	// BUG: There's a bug in the LAPC-I implementation:
	// When allocating for rhythm part, or when allocating for a part that is using fewer partials than it has reserved,
	// held and playing polys on the rhythm part can potentially be aborted before releasing polys on the rhythm part.
	// This bug isn't present on MT-32.
	// I consider this to be a bug because I think that playing polys should always have priority over held polys,
	// and held polys should always have priority over releasing polys.

	// NOTE: This code generally aborts polys in parts (according to certain conditions) in the following order:
	// 7, 6, 5, 4, 3, 2, 1, 0, 8 (rhythm)
	// (from lowest priority, meaning most likely to have polys aborted, to highest priority, meaning least likely)

	if (needed == 0) {
		return true;
	}

	if (getFreePartialCount() >= needed) {
		return true;
	}

	for (;;) {
		// Abort releasing polys in non-rhythm parts that have exceeded their partial reservation (working backwards from part 7)
		if (!abortFirstReleasingPolyWhereReserveExceeded(0)) {
			break;
		}
		if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
			return true;
		}
	}

	if (parts[partNum]->getActiveNonReleasingPartialCount() + needed > numReservedPartialsForPart[partNum]) {
		// With the new partials we're freeing for, we would end up using more partials than we have reserved.
		if (synth->getPart(partNum)->getPatchTemp()->patch.assignMode & 1) {
			// Priority is given to earlier polys, so just give up
			return false;
		}
		// Only abort held polys in the target part and parts that have a lower priority
		// (higher part number = lower priority, except for rhythm, which has the highest priority).
		for (;;) {
			if (!abortFirstPolyPreferHeldWhereReserveExceeded(partNum)) {
				break;
			}
			if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
				return true;
			}
		}
		if (needed > numReservedPartialsForPart[partNum]) {
			return false;
		}
	} else {
		// At this point, we're certain that we've reserved enough partials to play our poly.
		// Check all parts from lowest to highest priority to see whether they've exceeded their
		// reserve, and abort their polys until we have enough free partials or they're within
		// their reserve allocation.
		for (;;) {
			if (!abortFirstPolyPreferHeldWhereReserveExceeded(-1)) {
				break;
			}
			if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
				return true;
			}
		}
	}

	// Abort polys in the target part until there are enough free partials for the new one
	for (;;) {
		if (!parts[partNum]->abortFirstPolyPreferHeld()) {
			break;
		}
		if (synth->isAbortingPoly() || getFreePartialCount() >= needed) {
			return true;
		}
	}

	// Aww, not enough partials for you.
	return false;
}

CM32_HOT const Partial *PartialManager::getPartial(unsigned int partialNum) const {
	if (partialNum > synth->getPartialCount() - 1) {
		return NULL;
	}
	return partialTable[partialNum];
}

CM32_HOT Poly *PartialManager::assignPolyToPart(Part *part) {
	if (firstFreePolyIndex < synth->getPartialCount()) {
		Poly *poly = freePolys[firstFreePolyIndex];
		freePolys[firstFreePolyIndex] = NULL;
		firstFreePolyIndex++;
		poly->setPart(part);
		return poly;
	}
	return NULL;
}

CM32_HOT void PartialManager::polyFreed(Poly *poly) {
	if (0 == firstFreePolyIndex) {
		synth->printDebug("PartialManager Error: Cannot return freed poly, currently active polys:\n");
		for (Bit32u partNum = 0; partNum < 9; partNum++) {
			const Poly *activePoly = synth->getPart(partNum)->getFirstActivePoly();
			Bit32u polyCount = 0;
			while (activePoly != NULL) {
				activePoly = activePoly->getNext();
				polyCount++;
			}
			synth->printDebug("Part: %i, active poly count: %i\n", partNum, polyCount);
		}
	} else {
		firstFreePolyIndex--;
		freePolys[firstFreePolyIndex] = poly;
	}
	poly->setPart(NULL);
}

CM32_HOT void PartialManager::partialDeactivated(int partialIndex) {
	if (inactivePartialCount < synth->getPartialCount()) {
		inactivePartials[inactivePartialCount++] = partialIndex;
		return;
	}
	synth->printDebug("PartialManager Error: Cannot return deactivated partial %d, current partial state:\n", partialIndex);
	for (Bit32u i = 0; i < synth->getPartialCount(); i++) {
		const Partial *partial = partialTable[i];
		synth->printDebug("[Partial %d]: activation=%d, owner part=%d\n", i, partial->isActive(), partial->getOwnerPart());
	}
}

} // namespace MT32Emu
