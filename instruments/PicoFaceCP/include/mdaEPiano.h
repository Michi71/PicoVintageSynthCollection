/*
  PicoMDAEPiano

  PicoMDAEPiano is a port of the MDA-EPiano sound engine
  (https://sourceforge.net/projects/mda-vst/) for the Raspberry PI PICO.
   
  Copyright 2008-2011 David Robillard <http://drobilla.net>
  Copyright 1999-2000 Paul Kellett (Maxim Digital Audio)

  This is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License,
  or (at your option) any later version.

  This software is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this software. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __mdaEPiano__
#define __mdaEPiano__

#include <string.h>
#include <stdint.h>

#ifdef MDA_HOST_BUILD
// Host-Test-Build (macOS/Linux): keine Pico-Audio-Subsystem.
// Benoetigte Makros werden hier oder via -D bereitgestellt.
#ifndef PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL
#define PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL 3
#endif
#ifndef SAMPLES_PER_BUFFER
#define SAMPLES_PER_BUFFER 16
#endif
#else
#include "audio_subsystem.h"
#endif

// I2S buffer parameters
#define I2S_BUFFERS         PICO_AUDIO_I2S_BUFFERS_PER_CHANNEL 
#define I2S_BUFFER_WORDS    SAMPLES_PER_BUFFER 
#define SAMPLING_RATE       (44100)

#define NPARAMS 12       //number of parameters
#define NPROGS   5       //number of programs
#ifndef MDA_NINSTR
#define MDA_NINSTR 6   //number of instruments (0..5)
#endif
#define NOUTS    2       //number of outputs
#define NVOICES 96       //max polyphony
#define SUSTAIN 128
#define SILENCE 0.0001f  //voice choking
#define WAVELEN 422414   //wave data bytes

// MDAEPiano parameter mapping
#define MDA_EP_DECAY 0
#define MDA_EP_RELEASE 1
#define MDA_EP_HARDNESS 2
#define MDA_EP_TREBLE 3
#define MDA_EP_PAN_TREM 4
#define MDA_EP_LFO_RATE 5
#define MDA_EP_VELOCITY_SENSE 6
#define MDA_EP_STEREO 7
#define MDA_EP_MAX_POLY 8
#define MDA_EP_TUNE 9
#define MDA_EP_DETUNE 10
#define MDA_EP_OVERDRIVE 11

class mdaEPianoProgram
{
  friend class mdaEPiano;
private:
  float param[NPARAMS];
  char  name[24];
};


struct VOICE  //voice state
{
  int32_t  delta;  //sample playback
  int32_t  dbase;  //unbent delta (pitch bend base)
  int32_t  frac;
  int32_t  pos;
  int32_t  end;
  int32_t  loop;

  float env;  //envelope
  float dec;

  float f0;   //first-order LPF
  float f1;
  float ff;

  float outl;
  float outr;
  int32_t  note; //remember what note triggered this
};


struct KGRP  //keygroup
{
  int32_t  root;  //MIDI root note
  int32_t  high;  //highest note
  int32_t  pos;
  int32_t  end;
  int32_t  loop;
};

class mdaEPiano
{
public:
	mdaEPiano(uint8_t nvoices);
	~mdaEPiano();
	
	void noteOn(int32_t note, int32_t velocity);
    void noteOff(int32_t note);
	void process(int16_t *outputs_r, int16_t *outputs_l);
    bool processMidiController(uint8_t data1, uint8_t data2);
    void setPitchBend(int32_t bend14); // 0..16383, center 8192, +/-2 semitones
	
	void resumeVoices();
    void resetVoices(void);
    void resetControllers(void);
    void stopVoices(void);
	
	void setPolyphony(uint8_t value);
    uint8_t getPolyphony(void);
	
	void setVolume(uint8_t value); 	// Volume Range 0..127
    uint8_t getVolume(void);
    
	int32_t getProgramCount();
	int32_t getProgram();
	void setProgram(int32_t program);
	void flashWriteProgram();
	int flashReadProgram(int32_t p);
	void setProgramName(char *name);
	void getProgramName(char *name);
	bool copyProgram (int32_t destination);
	
	int32_t getParameterCount();
	void setParameter(int32_t index, float value);
	float getParameter(int32_t index);
	void getParameterLabel(int32_t index, char *label);
	void getParameterDisplay(int32_t index, char *text);
	void getParameterName(int32_t index, char *text);
	void setSampleRate(float sampleRate);
	void setInstrument(int32_t instrument);       /* 0..MDA_NINSTR-1 (siehe mdaEPianoInstruments.h) */
	int32_t getCurrentInstrument();
	char*  getCurrentInstrumentName();
	int32_t getInstrumentCount();
	const char* getInstrumentName(int32_t idx);
	void setBlockSize(int32_t blockSize);

	/* Eigenes Instrument laden (data + Keygroup-Tabelle). data wird NUR gelesen
	   (Loop-Punkte muessen klickfrei sein; XFade entfaellt). kgrpTable muss die
	   Feld-Reihenfolge root/high/pos/end/loop (jeweils int32) haben = struct KGRP. */
	void loadInstrument(const int16_t* data, const KGRP* kgrpTable,
	                   int32_t nKeygroups, int32_t noteonStep);

	int32_t guiUpdate;
	void guiGetDisplay(int32_t index, char *label);

private:
	void update();  //my parameter update
	void fillpatch(int32_t p, const char *name, float p0, float p1, float p2, float p3, float p4,
				 float p5, float p6, float p7, float p8, float p9, float p10,float p11);

	mdaEPianoProgram* programs;
	float Fs, iFs;

	///global internal variables
	uint8_t max_polyphony;
    KGRP  kgrp[64];                 /* max. Keygroups (Instrumente koennen groesser sein als mda) */
    int32_t nKeygroups;            /* aktive Keygroup-Anzahl des geladenen Instruments */
    int32_t noteonStep;            /* Velocity-Layer pro Region (mda: 3) */
    VOICE* voice;
	int32_t  activevoices, poly;
	short *waves;
	float width;
	int32_t  size, sustain;
	float lfo0, lfo1, dlfo, lmod, rmod;
	float treb, tfrq, tl, tr;
	float tune, fine, random, stretch, overdrive;
	float muff, muffvel, sizevel, velsens, volume, modwhl;
	float bendf = 1.0f; //current pitch bend factor
	int32_t curProgram;
	KGRP   defaultKgrp[64];      /* gespeicherter mda-Default-Keygroup-Satz (Instrument 0) */
	int32_t defaultNKeygroups;   /* = 33 (mda) */
	int32_t currentInstr;        /* aktives Instrument 0..MDA_NINSTR-1 */
	char    instrName[8];        /* aktueller Instrument-Name */
};

#endif
