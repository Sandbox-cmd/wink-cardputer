// Wink's mouth and ears. One task owns the audio hardware: the speaker and the
// microphone share a line on this board, so it speaks or listens, never both.
#pragma once
#include <Arduino.h>
#include "os.h"

enum Chime : uint8_t { CH_BOOT, CH_LINK_UP, CH_LINK_DOWN, CH_NOTE, CH_ERROR, CH_PTT_ON, CH_PTT_OFF, CH_TICK, CH_TUNE, CH_WIN, CH_LOSE };

namespace voice {
void begin();
void say(const String &text);       // queue speech: a recording if the card has one, else the chip's voice
void hum(const String &text, Mood mood);   // its own words, hummed: the tune of the sentence with the mouth closed
void gesture(bool opening);         // the mouth opening (the whole mind arrives) or closing (it leaves)
void chime(Chime c);                // queue a small sound
void tone(uint16_t hz, uint16_t ms);// queue one tone
void hush();                        // stop speaking now and drop queued speech
bool busy();                        // something is queued or sounding
bool speaking();                    // speech is sounding right now
float mouth();                      // 0..1 loudness of the speech sounding now
uint32_t utterance();               // counts up each time speech starts sounding
int spokenChars();                  // how far into that utterance's text the voice is (estimate)

float micLevel();                   // 0..1 loudness of the room, when listening
bool micLoud();                     // true once per sudden loud sound
bool micVoice();                    // true once per stretch of sustained sound (someone talking)

// push-to-talk: while active the microphone is recorded at 16 kHz into a stream
void pttStart();
void pttStop();
bool pttActive();
size_t pttRead(uint8_t *dst, size_t maxBytes);   // 16-bit little-endian samples

// hands-free: when allowed, a stretch of speech in the room is recorded into the same
// stream by itself, a little of what came just before it included
void voxAllow(bool allow);
bool voxActive();
void voxHoldOff(uint32_t ms);                    // not now: a key was just pressed
String debug(int force = -1);                    // the hearing, in numbers

// speech arriving from the Mac as sound (16 kHz mu-law); the link task feeds it
uint32_t streamOpen();                             // a new stream is starting; returns its number
size_t streamSpace();                              // how many bytes can be fed right now
void streamFeed(const uint8_t *d, size_t n);
void streamDrop(uint32_t id);                      // nobody is going to play that stream
void sayStream(const String &text, uint32_t id, uint32_t totalBytes);   // play it, showing these words

void packReload();                  // look again for recordings on the card
int packCount();                    // how many recordings were found
const char *lastSource();           // where the last thing said came from

String speakable(const String &text);   // what the chip's voice is actually given
String words(long n);                   // 42 -> "forty two"
}
