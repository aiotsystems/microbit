/*
Star Wars: an orchestra of micro:bit boards, each playing one instrument of a piece
(the Star Wars main theme), all in sync.

=========================== user guide =========================================

Hold the board with the USB connector at the top.

The LEDs show the number of the track (the instrument) the board plays: its last
digit in the 3 right columns, and in the left column one LED (top) for 10, two
LEDs for 20. After a reset, the board is silent, on track 0 (Trumpet).

    #.###    track 12: one LED for 10,
    ....#    and a 2
    ..###
    ..#..
    ..###

Buttons:
- A        starts playing the track, from the beginning
- B        stops playing
- A+B      (both together, then release) selects the next track, after 27 comes 0.
           While playing, the new instrument comes in where the piece is, in
           sync with the other boards. The change happens ~0.4 s after the
           release, the time to see whether a second A+B follows.
- A+B A+B  (twice in a row, hidden feature) plays or stops all the boards: if
           this board is stopped, it sends "PLAY!!" over the radio and starts
           playing, otherwise it sends "STOP!!" and stops.

Radio: every board listens all the time. A frame "PLAY!!" does the same as button
A, "STOP!!" the same as button B; they can come from a board running this app
(A+B A+B) or any other sender, e.g. 00std_wireless_tx with that payload.

To play as an orchestra: on each board, select a different track with A+B, then
press A+B A+B on any one of them to start them all, and again to stop them all.
All the tracks last as long (80.3 s), the boards loop back to the beginning of
the piece together. script/select_tracks.py helps picking which tracks to use:
it lists them, plays them on the computer alone or together, and saves a
selection.

The tracks:
     0 Trumpet             10 Piccolo             20 Timpani F
     1 Tremolo Strings     11 Oboe                21 Cymbals
     2 Trombone            12 Clarinet            22 Other Percussion
     3 Brass               13 Bassoon             23 French Horn Classic
     4 French Horn         14 Concert Harp        24 Trombone Classic
     5 Tuba                15 Glock               25 Trumpet
     6 Tuba 2              16 Trumpet 2           26 Brass
     7 Strings             17 Snare               27 Violin Classic
     8 Strings 2           18 Timpani D
     9 Flute Classic       19 Timpani A

=========================== implementation =====================================

The songs. reference/Main_Theme_StarWars.mid is the piece. Two scripts turn it
into C:
- script/midi_to_csv.py writes one CSV per track (note, frequency_hz,
  duration_ms), reduced to one note at a time, as the speaker plays one note.
- script/csv_to_songs.py writes one header per track in songs/, an array of
  note_t { period, duration_ms } (see song.h), and songs/songs.h, which lists
  them all in tracks[] (track_t: notes, number of notes, name). It pads every
  track with silence to the length of the longest one, so they loop together,
  and orders them as in ORDER. All 28 tracks take ~33 kB of flash.

The sound. PWM0 drives the speaker (P0.00) with a square wave, duty cycle 50%
(pwm_comp = period/2). The PWM counter runs at 16 MHz with a prescaler of 4,
i.e. 16MHz/4=4MHz, so a note of freq_Hz has
    period = (4000000/freq_Hz)
(it would be 16000000/freq_Hz without prescaler). COUNTERTOP is 15 bits wide:
the lowest note is 4000000/32767 = 122 Hz, csv_to_song.py raises lower ones by
octaves. A period of 0 is a silence: the PWM is stopped.

The tempo. RTC0 counts the time since the start of the piece, at 32768 Hz
(1 ms = 32.768 ticks, MS_TO_RTC()). Its LF clock is synthesized from the 32 MHz
crystal (+/-40 ppm), not taken from the internal RC oscillator (~1%), so that
the boards keep the same tempo. CC[0] holds the time the note playing ends,
an absolute time in the piece, so the rounding of the durations never adds up;
when it is reached, RTC0_IRQHandler plays the next note (song_play_next()). At
the end of the track, the counter goes back to 0. Switching track while playing
(song_switch()) reads the counter and plays the note of the new track at that
time, for what is left of it.

The main loop runs every ~10 ms, the time leds_refresh_10ms() takes to light
the 5 rows of LEDs one after the other (~2 ms each). It then reads the buttons,
debounced (two readings in a row must agree), and acts once all buttons are
released, on all those that were pressed: pressing both never starts at
exactly the same time, this way it is not mistaken for A or B alone. A+B waits
DOUBLE_PRESS_LOOPS loops for a second A+B before selecting the next track.

The radio uses the settings of 00std_wireless_rx and 00std_wireless_tx: BLE
long range 125 kbit/s, frequency 2410 MHz (FREQUENCY=10), 8 dBm. It listens all
the time; RADIO_IRQHandler only notes a valid frame (CRC ok, exactly "PLAY!!" or
"STOP!!") in radio_command, and main() acts on it, as on the buttons, so the
song is only ever started or stopped from main(). radio_send() stops listening,
sends the frame RADIO_REPEAT times (~1 ms each), in case a board misses one,
and listens again.
*/

#include <string.h>
#include <nrf.h>
#include "nrf52833.h"

//=========================== defines =========================================

// all the tracks of the MIDI file, in flash: tracks[], see song.h
#include "songs/songs.h"

// the LED matrix: LED rc is on when row r is high and column c is low
const uint8_t LED_ROW_PINS[5]           = { 21, 22, 15, 24, 19 };                  // P0.21, P0.22, P0.15, P0.24, P0.19
const uint8_t LED_COL_PINS[5]           = { 28, 11, 31,  5, 30 };                  // P0.28, P0.11, P0.31, P1.05, P0.30
NRF_GPIO_Type* const LED_COL_PORTS[5]   = { NRF_P0, NRF_P0, NRF_P0, NRF_P1, NRF_P0 };

// the digits, 3 columns wide: they go in the 3 right columns
const uint8_t DIGITS[10][5] = {
    { 0x7, 0x5, 0x5, 0x5, 0x7 }, // 0  ###  #.#  #.#  #.#  ###
    { 0x2, 0x6, 0x2, 0x2, 0x7 }, // 1  .#.  ##.  .#.  .#.  ###
    { 0x7, 0x1, 0x7, 0x4, 0x7 }, // 2  ###  ..#  ###  #..  ###
    { 0x7, 0x1, 0x3, 0x1, 0x7 }, // 3  ###  ..#  .##  ..#  ###
    { 0x5, 0x5, 0x7, 0x1, 0x1 }, // 4  #.#  #.#  ###  ..#  ..#
    { 0x7, 0x4, 0x7, 0x1, 0x7 }, // 5  ###  #..  ###  ..#  ###
    { 0x7, 0x4, 0x7, 0x5, 0x7 }, // 6  ###  #..  ###  #.#  ###
    { 0x7, 0x1, 0x1, 0x2, 0x2 }, // 7  ###  ..#  ..#  .#.  .#.
    { 0x7, 0x5, 0x7, 0x5, 0x7 }, // 8  ###  #.#  ###  #.#  ###
    { 0x7, 0x5, 0x7, 0x1, 0x7 }, // 9  ###  #.#  ###  ..#  ###
};

// the buttons, low when pressed
#define BUTTON_A      14 // P0.14
#define BUTTON_B      23 // P0.23
#define PRESSED_A     0x01
#define PRESSED_B     0x02

// how long to wait after A+B for a second A+B (a double press), in loops of ~10 ms
#define DOUBLE_PRESS_LOOPS 40

// the radio: a frame "PLAY!!" starts playing, "STOP!!" stops, like buttons A and B
// (6 bytes, the length of the payload of 00std_wireless_tx)
#define RADIO_NONE    0
#define RADIO_PLAY    1
#define RADIO_STOP    2

// how many times radio_send() sends a frame, in case a board misses one
#define RADIO_REPEAT  3

// the RTC runs at 32768 Hz: 1 ms = 32.768 ticks
#define MS_TO_RTC(ms) (((uint32_t)(ms) * 32768) / 1000)

//=========================== variables =======================================

typedef struct {
    // the song
    volatile uint8_t track;           // the track being played, index in tracks[] (list at the top)
    uint16_t         beat;            // the next note to play, in tracks[track]
    uint32_t         next_ms;         // when the note playing ends, in ms since the start of the piece
    uint16_t         pwm_comp[1];     // the PWM duty cycle, read by the PWM peripheral
    // the LEDs: what they show, one byte per row (top to bottom), one bit per column:
    // 0x10 is the left column, 0x01 the right one (USB connector at the top)
    uint8_t          display[5];
    // the radio
    uint8_t          rx_pdu[2+8];     // received: header, length, up to 8 bytes of payload
    uint8_t          tx_pdu[2+6];     // to send: header, length, 6 bytes of payload
    volatile uint8_t radio_command;   // set by RADIO_IRQHandler, handled by main()
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes =========================================

// song
void    song_start(void);
void    song_switch(void);
void    song_play_next(void);
void    song_stop(void);
// leds
void    leds_display_track(uint8_t track);
void    leds_refresh_10ms(void);
// buttons
void    buttons_init(void);
uint8_t buttons_read(void);
// clocks
void    clocks_init(void);
// radio
void    radio_init(void);
uint8_t radio_received(const char* frame, uint8_t len);
void    radio_send(const char* frame);
// rtc
void    rtc_init(void);
// pwm
void    pwm_init(void);
void    pwm_setperiod(uint16_t period);

//=========================== main ===============================================

int main(void) {
    uint8_t playing  = 0;  // the song does not play until button A is pressed
    uint8_t previous = 0;  // buttons at the previous reading
    uint8_t current;       // buttons now
    uint8_t stable   = 0;  // buttons, debounced: same value at two readings in a row
    uint8_t gesture  = 0;  // all buttons pressed since they were all released
    uint8_t both     = 0;  // >0 after A+B: loops left to wait for a second A+B

    // all variables to zero, then the ones that start otherwise
    memset(&app_vars, 0, sizeof(app_vars));
    app_vars.track = 0; // Trumpet

    clocks_init();
    pwm_init();
    rtc_init();
    buttons_init();
    radio_init();
    leds_display_track(app_vars.track);

    while(1) {
        leds_refresh_10ms();

        // a button bounces for a few ms when pressed or released: ignore the
        // readings until two in a row agree
        current  = buttons_read();
        if(current==previous) stable = current;
        previous = current;

        gesture |= stable;

        // act once all buttons are released: pressing both never starts at
        // exactly the same time, this way it is not mistaken for A or B alone
        if(stable==0 && gesture!=0) {
            if(gesture==(PRESSED_A|PRESSED_B) && both>0) {
                // both, twice in a row (hidden): play or stop all the boards
                // over the radio, and this one, which does not hear itself
                both = 0;
                if(playing) {
                    radio_send("STOP!!");
                    song_stop();
                    playing = 0;
                } else {
                    radio_send("PLAY!!");
                    song_start();
                    playing = 1;
                }
            } else if(gesture==(PRESSED_A|PRESSED_B)) {
                // both: wait and see whether a second one follows
                both = DOUBLE_PRESS_LOOPS;
            } else if(gesture==PRESSED_A) {
                // A: start
                song_start();
                playing = 1;
            } else {
                // B: stop
                song_stop();
                playing = 0;
            }
            gesture = 0;
        }

        // no second A+B pressed in time (the wait pauses while a button is held):
        // it was a single one, next track, which comes in right away, where we are in
        // the piece, if we were playing
        if(both>0 && gesture==0 && --both==0) {
            app_vars.track = (app_vars.track+1)%NUM_TRACKS;
            leds_display_track(app_vars.track);
            if(playing) song_switch();
        }

        // the same as buttons A and B, sent over the radio
        if(app_vars.radio_command==RADIO_PLAY) {
            song_start();
            playing = 1;
        } else if(app_vars.radio_command==RADIO_STOP) {
            song_stop();
            playing = 0;
        }
        app_vars.radio_command = RADIO_NONE;
    }
}

//=========================== helpers ============================================

void song_start(void) {
    // play the current track from its beginning

    NVIC_DisableIRQ(RTC0_IRQn);
    NRF_RTC0->TASKS_STOP               = 0x00000001;
    NRF_RTC0->TASKS_CLEAR              = 0x00000001;
    NRF_RTC0->EVENTS_COMPARE[0]        = 0x00000000;
    NVIC_ClearPendingIRQ(RTC0_IRQn);

    app_vars.beat    = 0;
    app_vars.next_ms = 0;
    song_play_next();

    NRF_RTC0->TASKS_START              = 0x00000001;
    NVIC_EnableIRQ(RTC0_IRQn);
}

void song_switch(void) {
    // while playing, switch to the current track where we are in the piece:
    // the new instrument comes in in sync with the others

    const track_t* t = &tracks[app_vars.track];
    uint32_t now_ms;
    uint32_t start_ms;

    NVIC_DisableIRQ(RTC0_IRQn);

    // where we are in the piece
    now_ms   = (NRF_RTC0->COUNTER * 1000) / 32768;

    // the note of the new track playing at that time: the first one that does not end
    // before (with 1 ms to spare, so that its end is still ahead of the RTC)
    app_vars.beat     = 0;
    start_ms = 0;
    while(app_vars.beat<t->len && start_ms+t->notes[app_vars.beat].duration_ms <= now_ms+1) {
        start_ms += t->notes[app_vars.beat].duration_ms;
        app_vars.beat++;
    }

    // play it until its end (if we are at the very end of the piece, this wraps around)
    app_vars.next_ms  = start_ms;
    song_play_next();

    // the end of the note of the old track may have passed in the meantime: forget it
    NRF_RTC0->EVENTS_COMPARE[0]        = 0x00000000;
    NVIC_ClearPendingIRQ(RTC0_IRQn);
    NVIC_EnableIRQ(RTC0_IRQn);
}

void song_play_next(void) {
    // play note <beat>, and have the RTC interrupt when it is over: RTC0 counts the
    // time since the start of the piece, the note ends at next_ms in the piece
    // (an absolute time, so the rounding of the durations never adds up)

    const track_t* t = &tracks[app_vars.track];

    // wrap around at the end of the track: back to the start of the piece
    // (all the tracks last as long, see csv_to_songs.py, so all the boards do it together)
    if(app_vars.beat>=t->len) {
        app_vars.beat    = 0;
        app_vars.next_ms = 0;
        NRF_RTC0->TASKS_CLEAR          = 0x00000001;
    }

    pwm_setperiod(t->notes[app_vars.beat].period);
    app_vars.next_ms                  += t->notes[app_vars.beat].duration_ms;
    NRF_RTC0->CC[0]                    = MS_TO_RTC(app_vars.next_ms);
    app_vars.beat++;
}

void song_stop(void) {
    // stop playing, silence the speaker

    NVIC_DisableIRQ(RTC0_IRQn);
    NRF_RTC0->TASKS_STOP               = 0x00000001;
    NRF_RTC0->EVENTS_COMPARE[0]        = 0x00000000;
    NVIC_ClearPendingIRQ(RTC0_IRQn);

    pwm_setperiod(0);

    NVIC_EnableIRQ(RTC0_IRQn);
}

void leds_display_track(uint8_t track) {
    // show the track number: its last digit on the right, and in the left column
    // one LED (top) for 10, two LEDs for 20
    //   #.###    track 12
    //   ....#
    //   ..###
    //   ..#..
    //   ..###

    uint8_t r;

    for(r=0; r<5; r++) {
        app_vars.display[r] = DIGITS[track%10][r];
    }
    if(track>=10) app_vars.display[0] |= 0x10;
    if(track>=20) app_vars.display[1] |= 0x10;
}

void leds_refresh_10ms(void) {
    // light the LEDs in display[] for roughly 10 ms: one row at a time, ~2 ms each,
    // switching fast enough that the eye sees them all on

    uint8_t r;
    uint8_t c;

    for(r=0; r<5; r++) {
        // columns of the LEDs that are on in this row: output, low
        for(c=0; c<5; c++) {
            if(app_vars.display[r] & (0x10>>c)) {
                LED_COL_PORTS[c]->PIN_CNF[LED_COL_PINS[c]] = 0x00000003;
                LED_COL_PORTS[c]->OUTCLR                   = (0x00000001 << LED_COL_PINS[c]);
            }
        }
        // row: output, high
        NRF_P0->PIN_CNF[LED_ROW_PINS[r]]                   = 0x00000003;
        NRF_P0->OUTSET                                     = (0x00000001 << LED_ROW_PINS[r]);

        for(volatile uint32_t i=0; i<12800; i++);

        // all off: disconnect the row and the columns
        NRF_P0->PIN_CNF[LED_ROW_PINS[r]]                   = 0x00000002;
        for(c=0; c<5; c++) {
            LED_COL_PORTS[c]->PIN_CNF[LED_COL_PINS[c]]     = 0x00000002;
        }
    }
}

void buttons_init(void) {

    // configure P0.14 (button A) and P0.23 (button B) as inputs, the board has the pull-ups
    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ...A A: DIR:     0=Input
    // .... .... .... .... .... .... .... ..B. B: INPUT:   0=Connect
    // .... .... .... .... .... .... .... CC.. C: PULL:    0=Disabled
    // .... .... .... .... .... .DDD .... .... D: DRIVE:   0=S0S1
    // .... .... .... ..EE .... .... .... .... E: SENSE:   0=Disabled
    // xxxx xxxx xxxx xx00 xxxx x000 xxxx 0000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_P0->PIN_CNF[BUTTON_A]     = 0x00000000;
    NRF_P0->PIN_CNF[BUTTON_B]     = 0x00000000;
}

uint8_t buttons_read(void) {
    // which buttons are pressed right now (PRESSED_A, PRESSED_B, or both)

    uint8_t pressed = 0;
    if((NRF_P0->IN & (1<<BUTTON_A))==0) pressed |= PRESSED_A;
    if((NRF_P0->IN & (1<<BUTTON_B))==0) pressed |= PRESSED_B;
    return pressed;
}

void clocks_init(void) {

    // HF crystal: the radio needs it, and the LF clock is made from it
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {}

    // LF clock, which drives RTC0 and so the tempo: synthesized from the HF crystal
    // (+/-40 ppm), not the internal RC oscillator (~1%, i.e. up to a second of
    // difference between two boards over the piece)
    NRF_CLOCK->TASKS_LFCLKSTOP    = 1;  // its source can only change while it is stopped
    while (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) {}
    NRF_CLOCK->LFCLKSRC           = (CLOCK_LFCLKSRC_SRC_Synth << CLOCK_LFCLKSRC_SRC_Pos);
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART   = 1;
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0) {}
}

void radio_init(void) {
    // listen, all the time, with the same settings as 00std_wireless_tx

    // configure radio
    NRF_RADIO->MODE          = (  RADIO_MODE_MODE_Ble_LR125Kbit << RADIO_MODE_MODE_Pos);
    NRF_RADIO->TXPOWER       = (  RADIO_TXPOWER_TXPOWER_Pos8dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->PCNF0         = (                              8 << RADIO_PCNF0_LFLEN_Pos)          |
                               (                              1 << RADIO_PCNF0_S0LEN_Pos)          |
                               (                              0 << RADIO_PCNF0_S1LEN_Pos)          |
                               (                              2 << RADIO_PCNF0_CILEN_Pos)          |
                               (     RADIO_PCNF0_PLEN_LongRange << RADIO_PCNF0_PLEN_Pos)           |
                               (                              3 << RADIO_PCNF0_TERMLEN_Pos);
    NRF_RADIO->PCNF1         = (              (sizeof(app_vars.rx_pdu) - 2) << RADIO_PCNF1_MAXLEN_Pos)         |
                               (                              0 << RADIO_PCNF1_STATLEN_Pos)        |
                               (                              3 << RADIO_PCNF1_BALEN_Pos)          |
                               (      RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos)         |
                               (   RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos);
    NRF_RADIO->BASE0         = 0xAAAAAAAAUL;
    NRF_RADIO->TXADDRESS     = 0UL;
    NRF_RADIO->RXADDRESSES   = (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->TIFS          = 0;
    NRF_RADIO->CRCCNF        = (         RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos)           |
                               (     RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO->CRCINIT       = 0xFFFFUL;
    NRF_RADIO->CRCPOLY       = 0x00065b; // CRC poly: x^16 + x^12^x^5 + 1
    NRF_RADIO->FREQUENCY     = 10;
    NRF_RADIO->PACKETPTR     = (uint32_t)app_vars.rx_pdu;

    // receive, and go back to receiving after each frame
    NRF_RADIO->SHORTS = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                        (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos) |
                        (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos);

    NRF_RADIO->INTENCLR = 0xffffffff;
    NRF_RADIO->INTENSET = (RADIO_INTENSET_DISABLED_Enabled << RADIO_INTENSET_DISABLED_Pos);
    NVIC_EnableIRQ(RADIO_IRQn);

    NRF_RADIO->TASKS_RXEN    = 1;
}

uint8_t radio_received(const char* frame, uint8_t len) {
    // is the payload received exactly <frame>?

    uint8_t i;

    if(app_vars.rx_pdu[1]!=len) return 0;
    for(i=0; i<len; i++) {
        if(app_vars.rx_pdu[2+i]!=frame[i]) return 0;
    }
    return 1;
}

void radio_send(const char* frame) {
    // send <frame> (6 bytes) RADIO_REPEAT times, back to back (~1 ms each), then go back to listening

    uint8_t i;
    uint8_t n;

    app_vars.tx_pdu[0] = 0x00; // header
    app_vars.tx_pdu[1] = 6;    // length
    for(i=0; i<6; i++) {
        app_vars.tx_pdu[2+i] = frame[i];
    }

    // stop listening, without RADIO_IRQHandler seeing it
    NVIC_DisableIRQ(RADIO_IRQn);
    NRF_RADIO->SHORTS        = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                               (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos);
    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {}

    // send: READY->START and END->DISABLE, so the frame is out once DISABLED
    NRF_RADIO->PACKETPTR       = (uint32_t)app_vars.tx_pdu;
    for(n=0; n<RADIO_REPEAT; n++) {
        NRF_RADIO->EVENTS_DISABLED = 0;
        NRF_RADIO->TASKS_TXEN      = 1;
        while (NRF_RADIO->EVENTS_DISABLED == 0) {}
    }

    // listen again
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->PACKETPTR       = (uint32_t)app_vars.rx_pdu;
    NRF_RADIO->SHORTS        = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                               (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos) |
                               (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos);
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
    NRF_RADIO->TASKS_RXEN      = 1;
}

void rtc_init(void) {
    
    // configure/start the RTC
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // xxxx xxxx xxxx FEDC xxxx xxxx xxxx xxBA (C=compare 0)
    // 0000 0000 0000 0001 0000 0000 0000 0000 
    //    0    0    0    1    0    0    0    0 0x00010000
    NRF_RTC0->EVTENSET                 = 0x00010000;       // enable compare 0 event routing
    NRF_RTC0->INTENSET                 = 0x00010000;       // enable compare 0 interrupts

    // enable interrupts
    NVIC_SetPriority(RTC0_IRQn, 1);
    NVIC_ClearPendingIRQ(RTC0_IRQn);
    NVIC_EnableIRQ(RTC0_IRQn);

    // CC[0] is set note by note by song_play_next(), RTC0 is started by song_start()
}

void pwm_init(void) {
    
    // configure P0.00 as output
    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ...A A: DIR:     1=Output
    // .... .... .... .... .... .... .... ..B. B: INPUT:   1=Disconnect
    // .... .... .... .... .... .... .... CC.. C: PULL:    0=Disabled
    // .... .... .... .... .... .DDD .... .... D: DRIVE:   0=S0S1
    // .... .... .... ..EE .... .... .... .... E: SENSE:   0=Disabled
    // xxxx xxxx xxxx xx00 xxxx xxxx xxxx 0011 
    //    0    0    0    0    0    0    0    3 0x00000003
    NRF_P0->PIN_CNF[0]            = 0x00000003;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... ...A AAAA A: PIN:      00 (P0.00)
    // .... .... .... .... .... .... ..B. .... B: PORT:     0  (P0.00)
    // C... .... .... .... .... .... .... .... C: CONNECT:  0=Connected
    // 0xxx xxxx xxxx xxxx xxxx xxxx xx00 0000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->PSEL.OUT[0]         = 0x00000000;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ...A A: ENABLE:   1=Enabled
    // 0xxx xxxx xxxx xxxx xxxx xxxx xxxx xxx1 
    //    0    0    0    0    0    0    0    1 0x00000001
    NRF_PWM0->ENABLE              = 0x00000001;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ...A A: UPDOWN:   0=Up
    // 0xxx xxxx xxxx xxxx xxxx xxxx xxxx xxx0 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->MODE                = 0x00000000;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... .AAA A: PRESCALER: 0=DIV_1
    // 0xxx xxxx xxxx xxxx xxxx xxxx xxxx x000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->PRESCALER           = 2;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... AAAA AAAA AAAA AAAA A: CNT:      0=Disabled
    // 0xxx xxxx xxxx xxxx 0000 0000 0000 0000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->LOOP                = 0x00000000;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ..AA A: LOAD:     0=Common
    // .... .... .... .... .... ...B .... .... B: MODE:     0=RefreshCount
    // 0xxx xxxx xxxx xxxx xxxx xxx0 xxxx xx00 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->DECODER             = 0x00000000;
    NRF_PWM0->SEQ[0].PTR          = (uint32_t)(app_vars.pwm_comp);
    NRF_PWM0->SEQ[0].CNT          = (sizeof(app_vars.pwm_comp) / sizeof(uint16_t));

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... AAAA AAAA AAAA AAAA AAAA AAAA A: CNT:      0=Continuous
    // 0xxx xxxx 0000 0000 0000 0000 0000 0000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->SEQ[0].REFRESH      = 0;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... AAAA AAAA AAAA AAAA AAAA AAAA A: CNT
    // 0xxx xxxx 0000 0000 0000 0000 0000 0000 
    //    0    0    0    0    0    0    0    0 0x00000000
    NRF_PWM0->SEQ[0].ENDDELAY     = 0;
}

void pwm_setperiod(uint16_t period) {

    if(NRF_PWM0->EVENTS_SEQSTARTED[0]==1) {
        NRF_PWM0->EVENTS_SEQSTARTED[0]=0;
        NRF_PWM0->EVENTS_STOPPED  = 0;
        NRF_PWM0->TASKS_STOP      = 0x00000001;
        while(NRF_PWM0->EVENTS_STOPPED==0);
    }

    // silence: leave the PWM stopped
    if(period==0) return;

    NRF_PWM0->COUNTERTOP          = period;
    app_vars.pwm_comp[0]          = period/2;

    NRF_PWM0->EVENTS_SEQSTARTED[0]=0;
    NRF_PWM0->TASKS_SEQSTART[0]   = 0x00000001;
    while(NRF_PWM0->EVENTS_SEQSTARTED[0]==0);
}

//=========================== interrupts ======================================

void RTC0_IRQHandler(void) {
    
    if (NRF_RTC0->EVENTS_COMPARE[0] == 0x00000001 ) {
        // handle compare[0]

        // clear flag
        NRF_RTC0->EVENTS_COMPARE[0]    = 0x00000000;

        // COUNTER keeps counting: it is the time in the piece
        song_play_next();
    }
}

void RADIO_IRQHandler(void) {
    if (NRF_RADIO->EVENTS_DISABLED) {
        NRF_RADIO->EVENTS_DISABLED = 0;

        // only note the command: main() acts on it, as it does for the buttons
        if (NRF_RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk) {
            if      (radio_received("PLAY!!", 6)) app_vars.radio_command = RADIO_PLAY;
            else if (radio_received("STOP!!", 6)) app_vars.radio_command = RADIO_STOP;
        }
    }
}
