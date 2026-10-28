#include "nrf52833.h"

// period = (16000000/freq_Hz)

// we use a prescaler of 4, so the PWM counter runs at 16MHz/4=4MHz
// period = (4000000/freq_Hz)

// all the tracks of the MIDI file, in flash: tracks[], see song.h
#include "songs/songs.h"

// the track being played, index in tracks[]; press both buttons for the next one:
//    0 Tremolo Strings
//    1 Trombone
//    2 Brass
//    3 French Horn
//    4 Trumpet
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
volatile uint8_t track = 4; // Trumpet

// the RTC runs at 32768 Hz: 1 ms = 32.768 ticks
#define MS_TO_RTC(ms) (((uint32_t)(ms) * 32768) / 1000)

uint16_t beat;

// the buttons, low when pressed
#define BUTTON_A      14 // P0.14
#define BUTTON_B      23 // P0.23
#define PRESSED_A     0x01
#define PRESSED_B     0x02

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

// busy wait, roughly 10 ms
void wait_10ms(void) {
    for(volatile uint32_t i=0; i<64000; i++);
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

int main(void) {
    uint8_t playing  = 0;  // the song does not play until button A is pressed
    uint8_t previous = 0;  // buttons at the previous reading
    uint8_t current;       // buttons now
    uint8_t stable   = 0;  // buttons, debounced: same value at two readings in a row
    uint8_t gesture  = 0;  // all buttons pressed since they were all released

    pwm_init();
    rtc_init();
    buttons_init();

    while(1) {
        wait_10ms();

        // a button bounces for a few ms when pressed or released: ignore the
        // readings until two in a row agree
        current  = buttons_read();
        if(current==previous) stable = current;
        previous = current;

        gesture |= stable;

        // act once all buttons are released: pressing both never starts at
        // exactly the same time, this way it is not mistaken for A or B alone
        if(stable==0 && gesture!=0) {
            if(gesture==(PRESSED_A|PRESSED_B)) {
                // both: next track, which plays right away if we were playing
                track = (track+1)%NUM_TRACKS;
                if(playing) song_start();
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