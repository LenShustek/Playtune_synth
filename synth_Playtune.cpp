/*****************************************************************************************************************

   synth_Playtune

   This an audio object for the PJRC Teensy Audio Library that reads an encoded "Playtune" bytestream
   created from a MIDI file. It has up to 16 simultaneous sound generators that are internally mixed to
   produce one monophonic audio stream.

   Sounds are created from sampled one-cycle waveforms for any number of instruments, each with its own
   attack-hold-decay-sustain-release envelope. Percussion sounds (from MIDI channel 10) are generated from
   longer sampled waveforms of a complete instrument strike. Each generator's volume is independently
   adjusted according to the MIDI velocity of the note being played before all channels are mixed.

   The public member functions of this class are:

     begin(const byte *bytestream)
        Play the specified bytestream, which we expect to be in PROGMEM (flash) memory.

     begin(const byte *bytestream, unsigned int num_gens)
        Play the specified bytesteam using num_gens sound generators.
        This is helpful only for old Playtune bytestream files that don't contain this information.

     isPlaying()
        Return true if the bytestream is still playing.

     stop()
        Stop playing the bytestream now.

   There are instructions in the code for adding more regular and percussion instruments,
   for changing the AHDSR amplitude envelope, and for changing the mixer levels.

   The bytestream is a compact series of commands that turn notes on and off, start a waiting
   period until the next note change, and specify instruments. The details are below.
   The easiest way to create the bytestream from a MIDI file is to use the Miditones program,
   which is open source at https://github.com/lenshustek/miditones.
   The best options to use for this version of Playtune are: -v -i -pt -d, and also -tn if you
   want to generate notes on more than the default 6 channels.

   This is the latest in a series of Playtune music generators for Arduino and Teensy
   microcontrollers dating back to 2011. Here are links to some of the others:
      https://github.com/LenShustek/arduino-playtune
      https://github.com/LenShustek/ATtiny-playtune
      https://github.com/LenShustek/playtune_poll
      https://github.com/LenShustek/playtune_samp

  -- Len Shustek, 23 August 2016
**********************************************************************************************************
   The MIT License (MIT)
   Copyright (c) 2016, Len Shustek

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
  associated documentation files (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or
  substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
  NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**********************************************************************************************************/
/*  Change log

  23 August 2016, L.Shustek, V1.0
     - Initial beta release.

*/

/*****  Format of the Playtune score bytestream

   The bytestream is a compact series of commands that turn notes on and off, start a waiting
   period until the next note change, and specify instruments. Here are the details, with
   numbers shown in hexadecimal.

   If the high-order bit of a byte is 1, then it is one of the following commands:

     9t nn  Start playing note nn on tone generator t.  Generators are numbered
            starting with 0.  The notes numbers are the MIDI numbers for the chromatic
            scale, with decimal 60 being Middle C, and decimal 69 being Middle A
            at 440 Hz.  The highest note is decimal 127 at about 12,544 Hz. except
            that percussion notes (instruments, really) range from 128 to 255.

            [vv]  If ASSUME_VOLUME is set to 1, or if the file header tells us to,
            then we expect a third byte with the volume ("velocity") value from 1 to
            127. You can generate this from Miditones with the -v option.
            (Everything breaks for headerless files if the assumption is wrong!)

     8t     Stop playing the note on tone generator t.

     Ct ii  Change tone generator t to play instrument ii from now on. Miditones will
            generate this with the -i option.

     F0     End of score: stop playing.

     E0     End of score: start playing again from the beginning.

   If the high-order bit of the byte is 0, it is a command to wait.  The other 7 bits
   and the 8 bits of the following byte are interpreted as a 15-bit big-endian integer
   that is the number of milliseconds to wait before processing the next command.
   For example,  07 D0  would cause a wait of 0x07d0 = 2000 decimal millisconds or 2
   seconds.  Any tones that were playing before the wait command will continue to play.

   Playtune bytestream files generated by later version of the Miditones progam using
   the -d option begin with a small header that describe what optional data is present
   in the file. This makes the file more self-describing, and this version of Playtune
   uses that if it is present.

    'Pt'   2 ascii characters that signal the presence of the header
     nn    The length (in one byte) of the entire header, 6..255
     ff1   A byte of flag bits, three of which are currently defined:
               80 velocity information is present
               40 instrument change information is present
               20 translated percussion notes are present
     ff2    Another byte of flags, currently undefined
     tt     The number (in one byte) of tone generators actually used in this music.
            We use that the scale the volume when combining simulatneous notes.

     Any subsequent header bytes covered by the count, if present, are currently undefined
     and are ignored.
*/


#include "Arduino.h"
#include "synth_Playtune.h"
#include "utility/dspinst.h"

#define MIN_NOTE 21 // we only do the piano range
#define MAX_NOTE 108
#define NUM_NOTES (MAX_NOTE - MIN_NOTE + 1)

#define INT_MAX 0x7FFFFFFF

// well-tempered MIDI note frequencies, based on the 12th root of 2.
const uint32_t freq4096 [NUM_NOTES] PROGMEM = {   // note frequencies * 4096
  /* 21..108*/ 112640, 119338, 126434, 133952, 141918, 150356, 159297,
  168769, 178805, 189437, 200702, 212636, 225280, 238676, 252868,
  267905, 283835, 300713, 318594, 337539, 357610, 378874, 401403,
  425272, 450560, 477352, 505737, 535809, 567670, 601425, 637188,
  675077, 715219, 757749, 802807, 850544, 901120, 954703, 1011473,
  1071618, 1135340, 1202851, 1274376, 1350154, 1430439, 1515497,
  1605613, 1701088, 1802240, 1909407, 2022946, 2143237, 2270680,
  2405702, 2548752, 2700309, 2860878, 3030994, 3211227, 3402176,
  3604480, 3818814, 4045892, 4286473, 4541360, 4811404, 5097505,
  5400618, 5721755, 6061989, 6422453, 6804352, 7208960, 7637627,
  8091784, 8572947, 9082720, 9622807, 10195009, 10801236, 11443511,
  12123977, 12844906, 13608704, 14417920, 15275254, 16183568,
  17145893
};

// 16-channel mixer levels.  The same levels currently apply to all inputs.

extern const int32_t mixer_amplitude_fractions[MAX_TGENS + 1] = { //0..16 tone generators playing
  /* Fractional amount (times 2^16) to reduce tone generator volume based on how many tone generators we're mixing.
     We are pretty conservative, assuming that highs won't often be coincident and our clipping when it happens
     won't be too annoying. This is pretty arbitrary, and YMMV. */
#define fract16(x) ((int32_t)(x*65536.0))
  fract16(1.0), // when no generators are playing
  //        1             2             3             4             5             6            7              8
  fract16(1.0), fract16(.60), fract16(.50), fract16(.40), fract16(.30), fract16(.25), fract16(.23), fract16(.20),
  //        9            10            11            12            13           14             15            16
  fract16(.18), fract16(.16), fract16(.15), fract16(.14), fract16(.13), fract16(.12), fract16(.11), fract16(.10)
};

//***********  REGULAR AND PERCUSSION INSTRUMENTS  ****************

// To add a regular instrument, you must do ALL FOUR things below
// and keep the instruments in the same order in each.

// (1) add an external reference here to the wave table that you put in synth_Playtune_waves.c

extern const int16_t waveform_aguitar_0033[256] PROGMEM;
extern const int16_t waveform_altosax_0001[256] PROGMEM;
extern const int16_t waveform_birds_0011[256] PROGMEM;
extern const int16_t waveform_cello_0005[256] PROGMEM;
extern const int16_t waveform_clarinett_0001[256] PROGMEM;
extern const int16_t waveform_clavinet_0021[256] PROGMEM;
extern const int16_t waveform_dbass_0015[256] PROGMEM;
extern const int16_t waveform_ebass_0037[256] PROGMEM;
extern const int16_t waveform_eguitar_0002[256] PROGMEM;
extern const int16_t waveform_eorgan_0064[256] PROGMEM;
extern const int16_t waveform_epiano_0044[256] PROGMEM;
extern const int16_t waveform_flute_0001[256] PROGMEM;
extern const int16_t waveform_oboe_0002[256] PROGMEM;
extern const int16_t waveform_piano_0013[256] PROGMEM;
extern const int16_t waveform_violin_0003[256] PROGMEM;

// (2) add an initializer for a new element in this array of structures.
//     It contains a pointer to the wave table, the DAHDSR envelope times in msec,
//     and the fraction of full volume that is the "sustain" volume.

struct instrument_waveform_t {
  const int16_t *waveforms; // pointer to the 256-element waveform array
  const int delay, attack, hold, decay, release;  // count of of samples for envelope each phase
  const int32_t sustain_level;  // envelope level for sustain, as a fraction * 2^16
#define ms2cnt(ms) ((int32_t)(ms*AUDIO_SAMPLE_RATE/1000)) // we define the delays as # of milliseconds
#define lv2fr(lv) ((int32_t)(lv*65536.0)) // and the level as a fraction * 2^16
#define DF_DL 0     // defaults in msec for delay, 
#define DF_AT 10    //   attack,
#define DF_HL 2     //   hold,
#define DF_DC 30    //   decay
#define DF_RL 30    //   release
#define DF_LV 0.60  // default for sustain amplitude level
} // some audio expert should tweak the envelope for each instrument independently!
instrument_waveforms[] = {// this order must match the enum below
  {waveform_aguitar_0033, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_altosax_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_birds_0011, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_cello_0005, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_clarinett_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_clavinet_0021, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_dbass_0015, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_ebass_0037, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_eguitar_0002, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_eorgan_0064, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_epiano_0044, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_flute_0001, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_oboe_0002, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)},
  {waveform_piano_0013, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(60), lv2fr(DF_LV)},
  {waveform_violin_0003, ms2cnt(DF_DL), ms2cnt(DF_AT), ms2cnt(DF_HL), ms2cnt(DF_DC), ms2cnt(DF_RL), lv2fr(DF_LV)}
};

// (3) add a symbolic index name for your regular instrument at the end of this list

enum  instrument_index_t { // instrument indexes
  I_AGUITAR, I_SAX, I_BIRDS, I_CELLO, I_CLARINET, I_CLAVINET, I_DBASS, I_EBASS,
  I_EGUITAR, I_ORGAN, I_EPIANO, I_FLUTE, I_OBOE, I_PIANO, I_VIOLIN
};

// (4) change whatever entries in this MIDI patch map corresponding to regular instruments
// that you want your new wave sample to play for. (I didn't create enough different
// instruments, so some of these assignments are pretty random!)

const byte instrument_patch_map[128] PROGMEM = { // map from MIDI patch numbers to instrument indexes
  /*1-8: DBASS*/ I_DBASS, I_DBASS, I_EBASS, I_DBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS,
  /*9-16: chromatic percussion*/ I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET, I_CLAVINET,
  /*17-24: Organ*/ I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN,
  /*25-32: guitar*/ I_AGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_AGUITAR,
  /*33-40: bass*/ I_DBASS, I_EBASS, I_EBASS, I_DBASS, I_DBASS, I_DBASS, I_EBASS, I_EBASS,
  /*41-48: strings*/ I_VIOLIN, I_VIOLIN, I_CELLO, I_CELLO, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
  /*49-56: ensemble*/ I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
  /*57-66: brass*/ I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS, I_DBASS,
  /*65-72: reed*/ I_SAX, I_SAX, I_SAX, I_OBOE, I_OBOE, I_SAX, I_SAX, I_OBOE,
  /*73-80: pipe*/ I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE, I_FLUTE,
  /*81-88: synth lead*/ I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR, I_EGUITAR,
  /*89-96: synth pad*/ I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN, I_VIOLIN,
  /*97-104: synth effects*/ I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS,
  /*105-112: ethnic*/ I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN, I_ORGAN,
  /*113-120: percussive*/ I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS, I_EBASS,
  /*121-128: sound effects*/ I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS, I_BIRDS
};

#if DO_PERCUSSION
// To add a percussion instrument, you must do ALL SIX things below
// and keep the instruments in order!

// (1) add an external reference here to the wave table you put in synth_Playtune_waves.c

extern const int16_t waveform_base_drum_04[] PROGMEM;
extern const int16_t waveform_snare_drum_1[] PROGMEM;
extern const int16_t waveform_mid_high_tom[] PROGMEM;
extern const int16_t waveform_cymbal_2[] PROGMEM;
extern const int16_t waveform_hi_bongo[] PROGMEM;
extern const int16_t waveform_steel_bell_c6[] PROGMEM;

// (2) put the pointer to your wave table at the end of this array, before the NULL

const int16_t *drum_waveforms[] = {
  waveform_base_drum_04, waveform_snare_drum_1, waveform_mid_high_tom,
  waveform_cymbal_2, waveform_hi_bongo, waveform_steel_bell_c6,
  NULL /* stopper so we can iterate over drum indexes */
};

// (3) put the size of it at the end of a table we refer to here that is
// actually located at the bottom of synth_Playtune_waves.c
extern const uint16_t drum_waveform_size[];

// (4) add an element to the end of this array telling what the sampling frequency is

const uint16_t drum_waveform_frequencies[] = {
  4000, 8000, 8000, 8000, 4000, 4000
};

// (5) add a symbolic index name for the percussion instrument at the end of this list

enum drum_index_t { // drum indexes
  D_BASS, D_SNARE, D_TOM, D_CYMBAL, D_BONGO, D_BELL
};

// (6) finally, change whatever entries in this patch map correspond to percussion instruments
// that want your new wave sample to play for.  These are note numbers on MIDI channel 10,
// or 9 if you start counting from zero as in the file binary data. (I didn't include too many
// percussion instruments, so some of these assignments are pretty random!)

const byte drum_patch_map[128] PROGMEM =  { // map from MIDI percussion instruments to drum indexes
  /*01-16*/ D_BASS, D_SNARE, D_TOM, D_CYMBAL, D_BONGO, D_BELL, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
  /*17-32*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
  /*33-48*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_SNARE, D_SNARE, D_SNARE, D_TOM, D_CYMBAL, D_TOM, D_CYMBAL, D_TOM, D_CYMBAL, D_TOM, D_TOM,
  /*49-64*/ D_CYMBAL, D_TOM, D_CYMBAL, D_CYMBAL, D_BELL, D_SNARE, D_CYMBAL, D_BELL, D_CYMBAL, D_CYMBAL, D_CYMBAL, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_BONGO,
  /*65-80*/ D_TOM, D_TOM, D_BELL, D_BELL, D_CYMBAL, D_CYMBAL, D_BELL, D_BELL, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_BONGO, D_TOM, D_TOM, D_BELL,
  /*81-96*/ D_BELL, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
  /*97-112*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS,
  /*113-128*/ D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS, D_BASS
};
#endif // DO_PERCUSSION

//------------------------------------------------------------------------------
//  Random byte generator
//
// This is an 8-bit version of the 2003 George Marsaglia XOR pseudo-random number
// generator. It has a full period of 255 before repeating.
// See http://www.arklyffe.com/main/2010/08/29/xorshift-pseudorandom-number-generator/
//------------------------------------------------------------------------------
static byte seed = 23;
byte random_byte(void) {
  seed ^= (byte)(seed << 7); // The casts are silly, but are needed to keep the AVR compiler
  seed ^= (byte)(seed >> 5); // from generating "mul 128" for "<< 7"! Shifts are faster.
  seed ^= (byte)(seed << 3);
  return seed;
}

//------------------------------------------------------------------------------
// Start playing a note on a particular tone generator
//------------------------------------------------------------------------------

void AudioSynthPlaytune::tune_playnote (byte tgen, byte note, byte vol) {
  struct tone_gen_t *tg;

  if (tgen < MAX_TGENS) {
    tg = &tone_gen[tgen];
    if (note >= 128) { // percussion instrument
#if DO_PERCUSSION
      int drum_enum = pgm_read_byte(drum_patch_map + note - 128);
      tg->waveform_array = drum_waveforms [drum_enum];
      //compute the increment to move from one sample point on the waveform to the next
      tg->tone_incr = (int64_t) drum_waveform_frequencies[drum_enum] * 0x20000 / AUDIO_SAMPLE_RATE;
      tg->tone_phase = 0; // start at the beginning
      tg->drum_ending_sample_index = drum_waveform_size[drum_enum] - 1; // remember the end of the waveform
      tg->percussion = true;
      // percussion notes generally seem undermodulated, so we might double the volume we get and clip
      if (BOOST_PERCUSSION) vol = vol > 63 ? 127 : vol << 1;
#if DO_ENVELOPE
      tg->env_mult = 0x10000;
      tg->env_incr = 0;
#endif

#if DBUG
      Serial.print("tgen="); Serial.print(tgen);
      Serial.print(" drum="); Serial.print(drum_enum);
      Serial.print(" vol="); Serial.print(vol);
      Serial.print(" incr="); Serial.print(tg->tone_incr);
      Serial.print(" phase="); Serial.println(tg->tone_phase);
#endif
#else //!DO_PERCUSSION
      return; // ignore percussion note
#endif
    }
    else  { // regular instrument
      if (note < MIN_NOTE) note = MIN_NOTE;
      if (note > MAX_NOTE) note = MAX_NOTE;
      int instrument_index = tg->instrument_index;
      tg->waveform_array = instrument_waveforms [instrument_index].waveforms;
#if DO_ENVELOPE
      tg->env_mult = 0; // setup AHDSR envelope
      tg->env_count = instrument_waveforms [instrument_index].delay; // # of samples
      // could be zero, but that will get dealt with at the first sample time.
      tg->env_state = ENV_DELAY;
      tg->env_incr = 0;
#endif
      //compute the increment to move from one sample point on the waveform to the next
      tg->tone_incr = ((uint64_t) (pgm_read_dword(freq4096 + (note - MIN_NOTE))) * 0x80000) / (uint64_t)AUDIO_SAMPLE_RATE;
      //start at random place in the wave cycle to minimize phase lock cancellations
      tg->tone_phase = random_byte() << 23;
      tg->percussion = false;
#if DBUG
      Serial.print("tgen="); Serial.print(tgen);
      Serial.print(" note="); Serial.print(note);
      Serial.print(" vol="); Serial.print(vol);
      Serial.print(" incr="); Serial.print(tg->tone_incr);
      Serial.print(" phase="); Serial.println(tg->tone_phase);
#endif
    }
    tg->volume_frac = ((int32_t)(vol & 0x7f) + 1) << 9; // 0x10000 to 0x0200
    //Serial.print("vol frac "); Serial.print(tg->volume_frac); Serial.print(" ampl frac "); Serial.println(amplitude_fraction);
    // tg->level = 0;
    tg->playing = true;  // go!
  }
}

//------------------------------------------------------------------------------
// Stop playing a note on a particular tone generator
//------------------------------------------------------------------------------

void AudioSynthPlaytune::tune_stopnote (byte tgen) {
  if (tgen < MAX_TGENS) {
    struct tone_gen_t *tg = &tone_gen[tgen];
    if (tg->playing) {
#if DBUG
      Serial.print("  stop tgen "); Serial.println(tgen);
#endif
#if DO_ENVELOPE
      if (!tg->percussion) {
        tg->env_state = ENV_RELEASE; // start release phase of a normal instrument note
        // ramp the amplitude from the sustain level down to 0
        tg->env_count = instrument_waveforms [tg->instrument_index].release;
        tg->env_mult = instrument_waveforms [tg->instrument_index].sustain_level;
        tg->env_incr = -tg->env_mult / tg->env_count; // ramp down to zero
        // when the count becomes zero, the sample update function will set tg->playing to false
      } else
#endif
        tg->playing = false;
    }
  }
}

//------------------------------------------------------------------------------
// Stop playing a score
//------------------------------------------------------------------------------

void AudioSynthPlaytune::tune_stopscore(void) {
  int i;
  for (i = 0; i < MAX_TGENS; ++i)
    tune_stopnote(i);
  tune_playing = false;
}

//------------------------------------------------------------------------------
//    Play a score
//------------------------------------------------------------------------------

void AudioSynthPlaytune::tune_playscore (const byte * score) { // start up the score
  if (tune_playing) stop();
  score_start = score;
  volume_present = ASSUME_VOLUME;
  num_tgens_used = MAX_TGENS;
  for (byte tgen = 0; tgen < MAX_TGENS; ++tgen) // set default instrument
    tone_gen[tgen].instrument_index = I_PIANO;

  // look for the optional file header
  memcpy_P(&file_header, score, sizeof(file_hdr_t)); // copy possible header from PROGMEM to RAM
  if (file_header.id1 == 'P' && file_header.id2 == 't') { // validate it
    volume_present = file_header.f1 & HDR_F1_VOLUME_PRESENT;
    num_tgens_used = max(1, min(MAX_TGENS, file_header.num_tgens));
#if DBUG
    Serial.print("header: volume_present="); Serial.print(volume_present);
    Serial.print(", #tonegens="); Serial.println(num_tgens_used);
#endif
    score_start += file_header.hdr_length; // skip the whole header
  }
  // We will attentuate amplitudes prior to combining notes based on the
  // worst-case number of notes that might be playing simultaneously.
  amplitude_fraction = mixer_amplitude_fractions[num_tgens_used];
#if DBUG
  Serial.print("amplitude fraction is "); Serial.println(amplitude_fraction);
#endif
  score_cursor = score_start;
  tune_stepscore();  /* execute initial the commands and return */
  tune_playing = true;
}

void AudioSynthPlaytune::tune_stepscore (void) { //*********   continue in the score
  byte cmd, opcode, tgen, note, vol;
  /* Do score commands until a "wait" is found, or the score is stopped.
    This is called initially from tune_playscore, but then is called
    from the slow interrupt routine when waits expire.
  */
  while (1) {
    cmd = pgm_read_byte(score_cursor++);
    if (cmd < 0x80) { /* wait count in msec. */
      /* wait count is in msec. */
      int scorewait_msec = ((unsigned)cmd << 8) | (pgm_read_byte(score_cursor++));
      scorewait_samples = (scorewait_msec * 1000 * (uint64_t)(AUDIO_SAMPLE_RATE + .5)) / 1000000;
#if DBUG
      Serial.print("wait samples = "); Serial.println(scorewait_samples);
#endif
      break;
    }
    opcode = cmd & 0xf0;
    tgen = cmd & 0x0f;
    if (opcode == CMD_STOPNOTE) { /* stop note */
      tune_stopnote (tgen);
    }
    else if (opcode == CMD_PLAYNOTE) { /* play note */
      note = pgm_read_byte(score_cursor++); // argument evaluation order is undefined in C!
      vol = volume_present ? pgm_read_byte(score_cursor++) : 127;
      tune_playnote (tgen, note, vol);
    }
    else if (opcode == CMD_INSTRUMENT) { /* change a tone generator's instrument */
      tone_gen[tgen].instrument_index = pgm_read_byte(instrument_patch_map + pgm_read_byte(score_cursor++));
    }
    else if (opcode == CMD_RESTART) { /* restart the score */
      score_cursor = score_start;
    }
    else if (opcode == CMD_STOP) { /* stop playing the score */
      stop();
      break;
    }
  }
}

/*************************************************************************************************
    Public interface functions
*************************************************************************************************/

void AudioSynthPlaytune::tune_init(void) {
}
void AudioSynthPlaytune::play(const byte *score) {
  play(score, MAX_TGENS);
}
void AudioSynthPlaytune::play(const byte *score, unsigned int num_tgens) {
  num_tgens_used = num_tgens;
  tune_playscore(score);
}
bool AudioSynthPlaytune::isPlaying(void) {
  return tune_playing;
}
void AudioSynthPlaytune::stop(void) {
  tune_stopscore();
}
// for testing...
void AudioSynthPlaytune::tune_setinstrument(byte tgen, byte instrument_index) {
  tone_gen[tgen].instrument_index = instrument_index;
}

/*************************************************************************************************
   Our interrupt-time "update" function, where all the dirty work gets done.
   We are called every 2.9 msec, and must generate a block of 128 2-byte samples
   as quickly as we can.
*************************************************************************************************/

void AudioSynthPlaytune::update(void) {
  audio_block_t *block = allocate();
  if (block) {
    for (int sample = 0; sample < AUDIO_BLOCK_SAMPLES; ++sample) { // for all samples
      // we use the sample processing interval (22.666 usec) as the timer for score waits
      if (tune_playing && scorewait_samples && --scorewait_samples == 0) {
        tune_stepscore ();  // end of a score wait, so execute more score commands
      }
#if DYNAMIC_VOLUME
      int num_tgens_playing = 0;
#endif
      int level = 0;
#if DYNAMIC_VOLUME // adjust the mixer input attentuation based on how many generators were last active
      amplitude_fraction = mixer_amplitude_fractions[num_tgens_playing_last];
#endif
      for (byte tgen = 0; tgen < num_tgens_used; ++tgen) { // look at each tone generator
        int our_level = 0;
        struct tone_gen_t *tg = &tone_gen[tgen];
        if (tg->playing) {
          uint32_t index1, index2, scale;
          int32_t val1, val2;
#if DYNAMIC_VOLUME
          ++num_tgens_playing;
#endif
          if (tg->percussion) { // percussion: play the waveform once
            // tone_phase = +iiiiiiiiiiiiiiffffffffffffffffx, i=index into waveform array, f=fraction
            index1 = tg->tone_phase >> 17; // 14 bits of index, 0..16383 max samples
            index2 = index1 + 1;
            if (index2 >= tg->drum_ending_sample_index)
              tg->playing = false; // end of percussion waveform; stop playing soon
            scale = (tg->tone_phase >> 1 ) & 0xFFFF; // 16 bits of fractional distance between samples
          }
          else { // regular instrument: repeat the waveform indefinitely
            // tone_phase = +iiiiiiiiffffffffffffffffxxxxxxx, i=index into waveform array, f=fraction
            index1 = tg->tone_phase >> 23; // 8 bits of index, 0..255 samples
            index2 = (index1 + 1) & 0xff;  // wrap around at the end
            scale = (tg->tone_phase >> 7) & 0xFFFF;  // 16 bits of fractional distance between samples
#if DO_ENVELOPE
            while (tg->env_count == 0) { // change to a state with a non-zero count
              switch (tg->env_state) {
                case ENV_IDLE:
                  tg->env_count = INT_MAX;
                  break;
                case ENV_DELAY:
                  tg->env_state = ENV_ATTACK;
                  tg->env_count = instrument_waveforms [tg->instrument_index].attack;
                  tg->env_incr = 0x10000 / tg->env_count; // ratchet up to maximum volume
                  break;
                case ENV_ATTACK:
                  tg->env_state = ENV_HOLD;
                  tg->env_count = instrument_waveforms [tg->instrument_index].hold;
                  tg->env_mult = 0x10000; // hold this volume
                  tg->env_incr = 0;
                  break;
                case ENV_HOLD:
                  tg->env_state = ENV_DECAY;
                  tg->env_count = instrument_waveforms [tg->instrument_index].decay;
                  tg->env_mult = 0x10000; // start with max volume
                  // count down to the sustain volume level
                  tg->env_incr = (instrument_waveforms [tg->instrument_index].sustain_level - 0x10000) / tg->env_count;
                  break;
                case ENV_DECAY:
                  tg->env_state = ENV_SUSTAIN;
                  tg->env_count = INT_MAX;
                  tg->env_mult = instrument_waveforms [tg->instrument_index].sustain_level;
                  tg->env_incr = 0; // maintain the sustain volume level
                  break;
                case ENV_SUSTAIN:
                  tg->env_count = INT_MAX; // (shouldn't happen; just keep on keeping on)
                  break;
                case ENV_RELEASE:
                  tg->env_state = ENV_IDLE;
                  tg->playing = false; // end of release: stop playing the note
                  break;
              }
            } // while state count is zero
            --tg->env_count; // count towards the next envelope state
#endif // DO_ENVELOPE
          } // regular instrument

          // do a linear interpolation between the samples that bracket the waveform point
          val1 = (int16_t)pgm_read_word(tg->waveform_array + index1);
          val1 *= 0xFFFF - scale;
          val2 = (int16_t)pgm_read_word(tg->waveform_array + index2);
          val2 *= scale;
          our_level = (val1 + val2) >> 16;
          tg->tone_phase = (tg->tone_phase + tg->tone_incr) & 0x7fffffff; // advance to the next waveform point
#if DO_ENVELOPE
          our_level = signed_multiply_32x16b(tg->env_mult, our_level);  // envelope amplitude attenuation
          tg->env_mult += tg->env_incr; // adjust attentuator
#endif
          // Mix all the tone generators together, scaling our current waveform amplitude by the volume of this
          // this note, attenuated by the number of tone generators that might be (or really are?) playing.
          level += signed_multiply_32x16b(tg->volume_frac, signed_multiply_32x16b(amplitude_fraction, our_level));

        } // tone generator is playing
      } // next tone generator

      block->data[sample] = level; // clips at -32768..+32767
#if DYNAMIC_VOLUME
      num_tgens_playing_last = num_tgens_playing; // for the next sample, remember how many generators were playing
#endif
    }
    transmit(block);
    release(block);
  }
}


