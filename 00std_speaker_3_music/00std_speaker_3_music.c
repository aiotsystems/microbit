#include <nrf.h>
#include "nrf52833.h"

// period = (16000000/freq_Hz)

// we use a prescaler of 4, so the PWM counter runs at 16MHz/4=4MHz
// period = (4000000/freq_Hz)

// all the tracks of the MIDI file, in flash: tracks[], see song.h
#include "songs/songs.h"

// the track being played, index in tracks[]; press both buttons for the next one:
//    0 Trumpet
//    1 Tremolo Strings
//    2 Trombone
//    3 Brass
//    4 French Horn
//    5 Trumpet 2
//    6 Tuba
//    7 Tuba 2
//    8 Strings
//    9 Strings 2
//   10 Flute Classic
//   11 Piccolo
//   12 Oboe
//   13 Clarinet
//   14 Bassoon
//   15 Glock
//   16 Concert Harp
//   17 Snare
//   18 Timpani D
//   19 Timpani A
//   20 Timpani F
//   21 Cymbals
//   22 Other Percussion
//   23 French Horn Classic
//   24 Trombone Classic
//   25 Trumpet
//   26 Brass
//   27 Violin Classic
volatile uint8_t track = 0; // Trumpet

// the RTC runs at 32768 Hz: 1 ms = 32.768 ticks
#define MS_TO_RTC(ms) (((uint32_t)(ms) * 32768) / 1000)

uint16_t beat;

// the buttons, low when pressed
#define BUTTON_A      14 // P0.14
#define BUTTON_B      23 // P0.23
#define PRESSED_A     0x01
#define PRESSED_B     0x02

// the LED matrix: LED rc is on when row r is high and column c is low
const uint8_t LED_ROW_PINS[5]           = { 21, 22, 15, 24, 19 };                  // P0.21, P0.22, P0.15, P0.24, P0.19
const uint8_t LED_COL_PINS[5]           = { 28, 11, 31,  5, 30 };                  // P0.28, P0.11, P0.31, P1.05, P0.30
NRF_GPIO_Type* const LED_COL_PORTS[5]   = { NRF_P0, NRF_P0, NRF_P0, NRF_P1, NRF_P0 };

// what the LEDs show, one byte per row (top to bottom), one bit per column:
// 0x10 is the left column, 0x01 the right one (USB connector at the top)
uint8_t display[5];

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

// the radio: a frame "PLAY!!" starts playing, "STOP!!" stops, like buttons A and B
// (6 bytes, the length of the payload of 00std_wireless_tx)
#define RADIO_NONE    0
#define RADIO_PLAY    1
#define RADIO_STOP    2

static uint8_t pdu[2+8];              // header, length, up to 8 bytes of payload
volatile uint8_t radio_command;       // set by RADIO_IRQHandler, handled by main()

uint16_t pwm_comp[1];

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
    NRF_PWM0->SEQ[0].PTR          = (uint32_t)(pwm_comp);
    NRF_PWM0->SEQ[0].CNT          = (sizeof(pwm_comp) / sizeof(uint16_t));

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
    pwm_comp[0]                   = period/2;

    NRF_PWM0->EVENTS_SEQSTARTED[0]=0;
    NRF_PWM0->TASKS_SEQSTART[0]   = 0x00000001;
    while(NRF_PWM0->EVENTS_SEQSTARTED[0]==0);
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

// which buttons are pressed right now (PRESSED_A, PRESSED_B, or both)
uint8_t buttons_read(void) {
    uint8_t pressed = 0;
    if((NRF_P0->IN & (1<<BUTTON_A))==0) pressed |= PRESSED_A;
    if((NRF_P0->IN & (1<<BUTTON_B))==0) pressed |= PRESSED_B;
    return pressed;
}

// show the track number: its last digit on the right, and in the left column
// one LED (top) for 10, two LEDs for 20
//   #.###    track 12
//   ....#
//   ..###
//   ..#..
//   ..###
void display_track(uint8_t t) {
    uint8_t r;

    for(r=0; r<5; r++) {
        display[r] = DIGITS[t%10][r];
    }
    if(t>=10) display[0] |= 0x10;
    if(t>=20) display[1] |= 0x10;
}

// light the LEDs in display[] for roughly 10 ms: one row at a time, ~2 ms each,
// switching fast enough that the eye sees them all on
void display_refresh_10ms(void) {
    uint8_t r;
    uint8_t c;

    for(r=0; r<5; r++) {
        // columns of the LEDs that are on in this row: output, low
        for(c=0; c<5; c++) {
            if(display[r] & (0x10>>c)) {
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

// play the current note, and have the RTC interrupt when its duration is over
void song_play_next(void) {
    const track_t* t = &tracks[track];

    // wrap around at the end of the track
    if(beat>=t->len) beat = 0;

    pwm_setperiod(t->notes[beat].period);
    NRF_RTC0->CC[0]                    = MS_TO_RTC(t->notes[beat].duration_ms);
    beat++;
}

// play the current track from its beginning
void song_start(void) {
    NVIC_DisableIRQ(RTC0_IRQn);
    NRF_RTC0->TASKS_STOP               = 0x00000001;
    NRF_RTC0->TASKS_CLEAR              = 0x00000001;
    NRF_RTC0->EVENTS_COMPARE[0]        = 0x00000000;
    NVIC_ClearPendingIRQ(RTC0_IRQn);

    beat = 0;
    song_play_next();

    NRF_RTC0->TASKS_START              = 0x00000001;
    NVIC_EnableIRQ(RTC0_IRQn);
}

// stop playing, silence the speaker
void song_stop(void) {
    NVIC_DisableIRQ(RTC0_IRQn);
    NRF_RTC0->TASKS_STOP               = 0x00000001;
    NRF_RTC0->EVENTS_COMPARE[0]        = 0x00000000;
    NVIC_ClearPendingIRQ(RTC0_IRQn);

    pwm_setperiod(0);

    NVIC_EnableIRQ(RTC0_IRQn);
}

// listen, all the time, with the same settings as 00std_wireless_tx
void radio_init(void) {

    // the radio needs the HF crystal
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {}

    // configure radio
    NRF_RADIO->MODE          = (  RADIO_MODE_MODE_Ble_LR125Kbit << RADIO_MODE_MODE_Pos);
    NRF_RADIO->TXPOWER       = (  RADIO_TXPOWER_TXPOWER_Pos8dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->PCNF0         = (                              8 << RADIO_PCNF0_LFLEN_Pos)          |
                               (                              1 << RADIO_PCNF0_S0LEN_Pos)          |
                               (                              0 << RADIO_PCNF0_S1LEN_Pos)          |
                               (                              2 << RADIO_PCNF0_CILEN_Pos)          |
                               (     RADIO_PCNF0_PLEN_LongRange << RADIO_PCNF0_PLEN_Pos)           |
                               (                              3 << RADIO_PCNF0_TERMLEN_Pos);
    NRF_RADIO->PCNF1         = (              (sizeof(pdu) - 2) << RADIO_PCNF1_MAXLEN_Pos)         |
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
    NRF_RADIO->PACKETPTR     = (uint32_t)pdu;

    // receive, and go back to receiving after each frame
    NRF_RADIO->SHORTS = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                        (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos) |
                        (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos);

    NRF_RADIO->INTENCLR = 0xffffffff;
    NRF_RADIO->INTENSET = (RADIO_INTENSET_DISABLED_Enabled << RADIO_INTENSET_DISABLED_Pos);
    NVIC_EnableIRQ(RADIO_IRQn);

    NRF_RADIO->TASKS_RXEN    = 1;
}

// is the payload of the frame received exactly <text>?
uint8_t radio_received(const char* text, uint8_t len) {
    uint8_t i;

    if(pdu[1]!=len) return 0;
    for(i=0; i<len; i++) {
        if(pdu[2+i]!=text[i]) return 0;
    }
    return 1;
}

// how many times radio_send() sends a frame, in case a board misses one
#define RADIO_REPEAT 3

// send <text> (6 bytes) RADIO_REPEAT times, back to back (~1 ms each), then go back to listening
void radio_send(const char* text) {
    static uint8_t tx_pdu[2+6];
    uint8_t i;
    uint8_t n;

    tx_pdu[0] = 0x00; // header
    tx_pdu[1] = 6;    // length
    for(i=0; i<6; i++) {
        tx_pdu[2+i] = text[i];
    }

    // stop listening, without RADIO_IRQHandler seeing it
    NVIC_DisableIRQ(RADIO_IRQn);
    NRF_RADIO->SHORTS        = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                               (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos);
    NRF_RADIO->TASKS_DISABLE = 1;
    while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {}

    // send: READY->START and END->DISABLE, so the frame is out once DISABLED
    NRF_RADIO->PACKETPTR       = (uint32_t)tx_pdu;
    for(n=0; n<RADIO_REPEAT; n++) {
        NRF_RADIO->EVENTS_DISABLED = 0;
        NRF_RADIO->TASKS_TXEN      = 1;
        while (NRF_RADIO->EVENTS_DISABLED == 0) {}
    }

    // listen again
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->PACKETPTR       = (uint32_t)pdu;
    NRF_RADIO->SHORTS        = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos) |
                               (RADIO_SHORTS_END_DISABLE_Enabled << RADIO_SHORTS_END_DISABLE_Pos) |
                               (RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos);
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
    NRF_RADIO->TASKS_RXEN      = 1;
}

// how long to wait after A+B for a second A+B (a double press), in loops of ~10 ms
#define DOUBLE_PRESS_LOOPS 40

int main(void) {
    uint8_t playing  = 0;  // the song does not play until button A is pressed
    uint8_t previous = 0;  // buttons at the previous reading
    uint8_t current;       // buttons now
    uint8_t stable   = 0;  // buttons, debounced: same value at two readings in a row
    uint8_t gesture  = 0;  // all buttons pressed since they were all released
    uint8_t both     = 0;  // >0 after A+B: loops left to wait for a second A+B

    pwm_init();
    rtc_init();
    buttons_init();
    radio_init();
    display_track(track);

    while(1) {
        display_refresh_10ms();

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
        // it was a single one, next track, which plays right away if we were playing
        if(both>0 && gesture==0 && --both==0) {
            track = (track+1)%NUM_TRACKS;
            display_track(track);
            if(playing) song_start();
        }

        // the same as buttons A and B, sent over the radio
        if(radio_command==RADIO_PLAY) {
            song_start();
            playing = 1;
        } else if(radio_command==RADIO_STOP) {
            song_stop();
            playing = 0;
        }
        radio_command = RADIO_NONE;
    }
}

void RTC0_IRQHandler(void) {
    
    if (NRF_RTC0->EVENTS_COMPARE[0] == 0x00000001 ) {
        // handle compare[0]

        // clear flag
        NRF_RTC0->EVENTS_COMPARE[0]    = 0x00000000;

        // clear COUNTER
        NRF_RTC0->TASKS_CLEAR          = 0x00000001;

        song_play_next();
    }
}

void RADIO_IRQHandler(void) {
    if (NRF_RADIO->EVENTS_DISABLED) {
        NRF_RADIO->EVENTS_DISABLED = 0;

        // only note the command: main() acts on it, as it does for the buttons
        if (NRF_RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk) {
            if      (radio_received("PLAY!!", 6)) radio_command = RADIO_PLAY;
            else if (radio_received("STOP!!", 6)) radio_command = RADIO_STOP;
        }
    }
}