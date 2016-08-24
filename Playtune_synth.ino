/*
    test program for synth_Playtune
*/

#include <Arduino.h>
#include <Audio.h>
#include "synth_Playtune.h"

AudioSynthPlaytune pt;
AudioOutputAnalog audioOut;
AudioConnection cord1(pt, audioOut);

void setup() {
  Serial.begin(115200);
  //while (!Serial) ; // wait for Arduino Serial Monitor?
  delay(200);
  Serial.print("Begin "); Serial.println(__FILE__);
  AudioMemory(18);
}

void playnote(byte note, byte instr, byte vol, int wait) { // (Uses hidden undocumented stuff!)
  pt.tune_setinstrument(0, instr);
  pt.tune_playnote(0, note, vol);
  pt.tune_playing = true;
  delay(wait);
  pt.tune_stopnote(0);
  delay(100);
}
void show_stats(void) {
  static unsigned long last_time = millis();
  if (1) { // show processor and memory utilization
    if (millis() - last_time >= 5000) {
      Serial.print("proc = ");
      Serial.print(AudioProcessorUsage());
      Serial.print("% (max ");
      Serial.print(AudioProcessorUsageMax());
      Serial.print("%), mem = ");
      Serial.print(AudioMemoryUsage());
      Serial.print(" blocks (max ");
      Serial.print(AudioMemoryUsageMax());
      Serial.println(")");
      last_time = millis();
    }
  }
}

void loop() {

#if 1 // play scores
#define playscore(s) extern const unsigned char PROGMEM s []; \
  pt.play(s); \
  while (pt.isPlaying()) show_stats();\
  delay(1000);

  playscore(MoneyMoney_score);
  playscore(UnsquareDance_score); // (lots of percussion)
  playscore(jordu_score);
#endif

#if 0 // a playground for various tests
  pt.num_tgens_used = 1; // maximize volume by mixing only one generator
  if (0)  for (byte note = 21; note <= 108; ++note) // play all notes
      playnote(note, 0, 127, 200);
  if (0)  for (byte instr = 0; instr <= 14; ++instr) // play all instruments
      playnote(60, instr, 127, 100);
  if (0) for (byte vol = 0; vol < 127; ++vol) // play all volumes
      playnote(60, 0, vol, 100);
  if (1)  for (byte drum = 128; drum <= 133; ++drum) // play all drums
      playnote(drum, 0, 127, 1000);
  if (0) for (byte tgens = 1; tgens <= 16; ++tgens) { // try all numbers of channels
      pt.num_tgens_used = tgens; // (this affects the mixer input attenuation)
      extern const int32_t mixer_amplitude_fractions[MAX_TGENS];
      pt.amplitude_fraction = mixer_amplitude_fractions[pt.num_tgens_used - 1];
      playnote(60, 0, 127, 100);
    }
  pt.tune_playing = false;
  delay(1000);
#endif

}
