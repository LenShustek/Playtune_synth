/* synth_Playtune.h

    The header file for synth_Playtune, an audio object for the PJRC Teensy Audio Library.
    See synth_Playtune.cpp for more information.

    Copyright (C) 2016, Len Shustek
*/

#ifndef synth_Playtune_h_
#define synth_Playtune_h_

#include "Arduino.h"
#include "AudioStream.h"

#define DBUG 0             // output console debugging messages?

#define MAX_TGENS 16        // maximum simultaneous tone generators
#define ASSUME_VOLUME 0     // assume volume information is present in bytestream files without headers?

#define DO_PERCUSSION 1     // generate code for percussion instruments?
#define BOOST_PERCUSSION 0  // amplify percussion instruments?
#define DO_ENVELOPE 1       // generate code to do DAHDSR tone amplitude envelope?
#define DYNAMIC_VOLUME 0    // dynamically adjust volume depending on how many instruments are playing?
//                          // (This is sometimes nice, but often sounds weird and exacerbates clipping distortion.)

struct file_hdr_t {  // the optional bytestream file header
  char id1;     // 'P'
  char id2;     // 't'
  unsigned char hdr_length; // length of whole file header
  unsigned char f1;         // flag byte 1
  unsigned char f2;         // flag byte 2
  unsigned char num_tgens;  // how many tone generators are used by this score
};
#define HDR_F1_VOLUME_PRESENT 0x80
#define HDR_F1_INSTRUMENTS_PRESENT 0x40
#define HDR_F1_PERCUSSION_PRESENT 0x20

// note commands in the bytestream
#define CMD_PLAYNOTE  0x90   /* play a note: low nibble is generator #, note is next byte, maybe volume */
#define CMD_STOPNOTE  0x80   /* stop a note: low nibble is generator # */
#define CMD_INSTRUMENT  0xc0 /* change instrument; low nibble is generator #, instrument is next byte */
#define CMD_RESTART 0xe0     /* restart the score from the beginning */
#define CMD_STOP  0xf0       /* stop playing */
/* if CMD < 0x80, then the other 7 bits and the next byte are a 15-bit big-endian number of msec to wait */

enum env_state_t {ENV_IDLE, ENV_DELAY, ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE};

class AudioSynthPlaytune : public AudioStream
{
  public:
    AudioSynthPlaytune(void) : AudioStream(0, NULL) {
      tune_init();
    }
    virtual void update(void);
    void play(const byte *);
    void play(const byte *, unsigned int);
    bool isPlaying(void);
    void stop(void);
    // the following should really be private, but are public temporarily for test code
    bool tune_playing = false;
    byte num_tgens_used = MAX_TGENS;
    void tune_playnote (byte tgen, byte note, byte vol);
    void tune_stopnote (byte tgen);
    void tune_setinstrument(byte tgen, byte instrument_index);
    int32_t amplitude_fraction = 0x10000;   // fraction of 2^16 to reduce amplitude by
  private:
    void tune_init(void);
    void tune_stopscore (void);
    void tune_stepscore (void);
    void tune_playscore (const byte * score);
    bool volume_present = ASSUME_VOLUME; // is there volume information in the bytestream?
    int num_tgens_playing_last = 0;      // how many tone generators played at the last sample
    const byte *score_start;             // the start of the Playtune bytestream
    const byte *score_cursor;            // where we are currently playing in the bytestream
    unsigned scorewait_samples = 0;      // how many samples to wait through for next score event
    struct tone_gen_t { // the internal state of each tone generator
      int32_t tone_phase;       // where we are playing in an instrument sample (2^16 fraction)
      int32_t tone_incr;        // increment from one sample to another (2^16 fraction)
      int32_t volume_frac;      // midi volume from 1..127 code (2^16 fraction)
      uint16_t drum_ending_sample_index; // the index of the last sample for a percussion instrument
      byte instrument_index;    // the instrument we're playing: I_PIANO, etc.
      byte playing;             // is this channel playing?
      byte percussion;          // is it a percussion instrument?
#if DO_ENVELOPE
      env_state_t env_state;      // envelope state variables:
      int32_t env_mult, env_incr; // amplitude multiplier and increment, as fractions * 2^16
      int env_count;              // duration count for this state, as number of samples
#endif
      const int16_t *waveform_array; // pointer to the waveform sample array
      //                                with 256 points for instruments, up to 16383 for percussion
    } tone_gen[MAX_TGENS];
    struct file_hdr_t file_header;  // a possible file header from the Playtune bytestream
};

#endif
