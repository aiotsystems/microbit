#include "nrf52833.h"

/*
sources:
    https://github.com/elecfreaks/circuitpython_cutebot/blob/main/cutebot.py
    https://github.com/Krakenus/microbit-cutebot-micropython/blob/master/cutebot.py
    https://github.com/bbcmicrobit/micropython/blob/master/source/microbit/microbiti2c.cpp
    https://microbit-micropython.readthedocs.io/en/latest/i2c.html#
    https://makecode.microbit.org/device/pins

I2C:
    config:
        7-bit addressing
    pins:
        freq: 100000
        SCL==micro:bit pin 19 (header)==P0.26 (nRF)
        SDA==micro:bit pin 20 (header)==P1.00 (nRF)
    address: 0x10,
    payload: [
        motor,     // 0x01: left,    0x02: right
        direction, // 0x02: forward, 0x01: backward
        speed,     // between 0 and 100
        0,
    ]
*/

#define SPEED 20

uint8_t BUF_LEFT_FWD[]   = {0x01,0x02,SPEED,0};
uint8_t BUF_LEFT_BACK[]  = {0x01,0x01,SPEED,0};
uint8_t BUF_LEFT_STOP[]  = {0x01,0x02,    0,0};
uint8_t BUF_RIGHT_FWD[]  = {0x02,0x02,SPEED,0};
uint8_t BUF_RIGHT_BACK[] = {0x02,0x01,SPEED,0};
uint8_t BUF_RIGHT_STOP[] = {0x02,0x02,    0,0};

void i2c_init(void) {
   //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... ...A A: DIR:   0=Input
    // .... .... .... .... .... .... .... ..B. B: INPUT: 1=Disconnect
    // .... .... .... .... .... .... .... CC.. C: PULL:  0=Disabled
    // .... .... .... .... .... .DDD .... .... D: DRIVE: 6=S0D1
    // .... .... .... ..EE .... .... .... .... E: SENSE: 0=Disabled
    // xxxx xxxx xxxx xx00 xxxx x110 xxxx 0010 
    //    0    0    0    0    0    6    0    2 0x00000602
    NRF_P0->PIN_CNF[26]           = 0x00000602; // SCL (P0.26)
    NRF_P1->PIN_CNF[0]            = 0x00000602; // SDA (P1.00)

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .... AAAA A: ENABLE: 5=Enabled
    // xxxx xxxx xxxx xxxx xxxx xxxx xxxx 0101 
    //    0    0    0    0    0    0    0    5 0x00000005
    NRF_TWI0->ENABLE              = 0x00000005;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... ...A AAAA A: PIN:    26 (P0.26)
    // .... .... .... .... .... .... ..B. .... B: PORT:    0 (P0.26)
    // C... .... .... .... .... .... .... .... C: CONNECT: 0=Connected
    // 0xxx xxxx xxxx xxxx xxxx xxxx xx01 1010 
    //    0    0    0    0    0    0    1    a 0x0000001a
    NRF_TWI0->PSEL.SCL            = 0x0000001a;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... ...A AAAA A: PIN:    00 (P1.00)
    // .... .... .... .... .... .... ..B. .... B: PORT:    1 (P1.00)
    // C... .... .... .... .... .... .... .... C: CONNECT: 0=Connected
    // 0xxx xxxx xxxx xxxx xxxx xxxx xx10 0000 
    //    0    0    0    0    0    0    2    0 0x00000020
    NRF_TWI0->PSEL.SDA            = 0x00000020;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA A: FREQUENCY: 0x01980000==K100==100 kbps
    NRF_TWI0->FREQUENCY           = 0x01980000;

    //  3           2            1           0
    // 1098 7654 3210 9876 5432 1098 7654 3210
    // .... .... .... .... .... .... .AAA AAAA A: ADDRESS: 16
    // xxxx xxxx xxxx xxxx xxxx xxxx x001 0000 
    //    0    0    0    0    0    0    1    0 0x00000010
    NRF_TWI0->ADDRESS             = 0x00000010;
}

void i2c_send(uint8_t* buf, uint8_t buflen) {
    uint8_t i;

    i=0;
    NRF_TWI0->TXD                 = buf[i];
    NRF_TWI0->EVENTS_TXDSENT      = 0;
    NRF_TWI0->TASKS_STARTTX       = 1;
    i++;
    while(i<buflen) {
        while(NRF_TWI0->EVENTS_TXDSENT==0);
        NRF_TWI0->EVENTS_TXDSENT  = 0;
        NRF_TWI0->TXD             = buf[i];
        i++;
    }
    while(NRF_TWI0->EVENTS_TXDSENT==0);
    NRF_TWI0->TASKS_STOP     = 1;
}

int main(void) {
    
    i2c_init();
    i2c_send(BUF_LEFT_FWD,  sizeof(BUF_LEFT_FWD));
    i2c_send(BUF_LEFT_BACK, sizeof(BUF_LEFT_BACK));
    i2c_send(BUF_LEFT_STOP, sizeof(BUF_LEFT_STOP));
    i2c_send(BUF_RIGHT_FWD, sizeof(BUF_RIGHT_FWD));
    i2c_send(BUF_RIGHT_BACK,sizeof(BUF_RIGHT_BACK));
    i2c_send(BUF_RIGHT_STOP,sizeof(BUF_RIGHT_STOP));

    while(1);
}
