#include "nrf52833.h"

// period = (16000000/freq_Hz)

// we use a prescaler of 4, so the PWM counter runs at 16MHz/4=4MHz
// period = (4000000/freq_Hz)

#define NOTE_NONE      0 // 246.94 Hz
#define NOTE_SI_2  16198 // 246.94 Hz
#define NOTE_DO_3  15289 // 261.63 Hz
#define NOTE_RE_3  13621 // 293.66 Hz
#define NOTE_MI_3  12135 // 329.63 Hz
#define NOTE_FA_3  11454 // 349.23 Hz
#define NOTE_SOL_3 10204 // 392.00 Hz
#define NOTE_LA_3   9091 // 440.00 Hz
#define NOTE_SI_3   8099 // 493.88 Hz
#define NOTE_DO_4   7645 // 523.25 Hz

uint16_t song[] = {
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_DO_3, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_LA_3,  NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_SOL_3, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_SOL_3, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_SOL_3, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_SOL_3, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
    NOTE_SI_2, NOTE_MI_3, NOTE_SOL_3, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE,
};
uint16_t beat;

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
        NRF_PWM0->EVENTS_STOPPED  = 0;
        NRF_PWM0->TASKS_STOP      = 0x00000001;
        while(NRF_PWM0->EVENTS_STOPPED==0);
    }

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

    // have RTC tick every second
    NRF_RTC0->CC[0]                    = 1317;             // 32768==1 s --> 1317==40.1ms
    NRF_RTC0->TASKS_START              = 0x00000001;       // start RTC0
}
    
int main(void) {
    pwm_init();
    rtc_init();

    while(1);
}

void RTC0_IRQHandler(void) {
    
    if (NRF_RTC0->EVENTS_COMPARE[0] == 0x00000001 ) {
        // handle compare[0]

        // clear flag
        NRF_RTC0->EVENTS_COMPARE[0]    = 0x00000000;

        // clear COUNTER
        NRF_RTC0->TASKS_CLEAR          = 0x00000001;

        pwm_setperiod(song[beat]);
        beat = (beat+1)%(sizeof(song) / sizeof(uint16_t));
    }
}