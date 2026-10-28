#ifndef SONG_H
#define SONG_H

#include <stdint.h>

// one note of a song: the (pitch, duration) pair
//   period      : PWM period (4000000/freq_Hz), 0 = silence
//   duration_ms : how long the note is held, in milliseconds
typedef struct {
    uint16_t period;
    uint16_t duration_ms;
} note_t;

// one track of the MIDI file: its notes, stored in flash
typedef struct {
    const note_t* notes;
    uint16_t      len;     // number of notes
    const char*   name;    // the instrument
} track_t;

#endif
