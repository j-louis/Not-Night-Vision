/*
 * File:    usb_comm.c
 * Author:  joshua
 * Created: April 15, 2016, 12:54 AM
 */

#include "hardware_config.h"

#include "usb.h"
#include "usb_device_hid.h"

#include "system.h"

#include <time.h>

#include "delay.h"

#include "piezo.h"
#include "laser.h"
#include "gimbal.h"
#include "mlx.h"

#include "usb_comm.h"

unsigned char txDataBuffer[BUF_SIZE];
unsigned char rxDataBuffer[BUF_SIZE];

volatile USB_HANDLE USBOutHandle;
volatile USB_HANDLE USBInHandle;

unsigned int txIdx;

time_t currentTime = 58008;
float msTimerMultiplier;

// packet information for different tx/rx items
// "address" part of packet stores information about what is being sent,
// the commands both from and to the host will contain an address that
// what the following data represents
// txBytes and rxBytes defines how many dataa bytes are sent or received
// for each corresponding address type
typedef struct { 
    const unsigned char address;
    const unsigned char txBytes;
    const unsigned char rxBytes;
} PacketInfo;

// 0x1* debugging commands
const PacketInfo CMD_BEEP           = { 0x10,   0,      1   };
const PacketInfo CMD_BEEP_LONG      = { 0x11,   0,      1   };   
const PacketInfo CMD_BEEP_BYTE      = { 0x12,   0,      1   };
const PacketInfo CMD_LED_TOG        = { 0x15,   1,      0   };
const PacketInfo CMD_LED_SET        = { 0x16,   0,      1   };
// 0x3* temperature sensor commands
const PacketInfo CMD_GET_TOBJ1      = { 0x30,   2+2+6,  0   };
const PacketInfo CMD_GET_TOBJ2      = { 0x31,   2+2+6,  0   };
const PacketInfo CMD_GET_TAMB       = { 0x32,   2+6,    0   };
// 0x4* timer commands
const PacketInfo CMD_SET_TIME       = { 0x40,   0,      4   };
const PacketInfo CMD_GET_TIME       = { 0x41,   4+2,    0   };
// 0x6* servo commands
const PacketInfo CMD_SET_PAN        = { 0x60,   0,      2   };  // (2 bytes) / 360 = resolution of 0.00549deg
const PacketInfo CMD_SET_TILT       = { 0x61,   0,      2   };  // (2 bytes) / 360 = resolution of 0.00549deg
// 0x7* laser activities
const PacketInfo CMD_LSR_TOG        = { 0x70,   1,      0   };
const PacketInfo CMD_LSR_SET        = { 0x71,   0,      1   };
// 0x8* delay commands
const PacketInfo CMD_DELAY_US       = { 0x80,   0,      2   };
const PacketInfo CMD_DELAY_MS       = { 0x81,   0,      2   };
// 0xe*
const PacketInfo CMD_GO_FLAG        = { 0x90,   0,      0   };
// 0xE* error identifiers


// macros for changing debug LED
#define mDEBUG_LED_OFF()    DEBUG_LED_TRIS&=~DEBUG_LED_MASK;\
                            DEBUG_LED_LAT&=~DEBUG_LED_MASK;
#define mDEBUG_LED_ON()     DEBUG_LED_TRIS&=~DEBUG_LED_MASK;\
                            DEBUG_LED_LAT|=DEBUG_LED_MASK;
#define mDEBUG_LED_TOGGLE() DEBUG_LED_TRIS&=~DEBUG_LED_MASK;\
                            DEBUG_LED_LAT^=DEBUG_LED_MASK;

void initUSBComm( void ) {
    
    // initialize handle for holding last tx
    USBInHandle = 0;

    // enable the HID endpoint
    USBEnableEndpoint( 
            CUSTOM_DEVICE_HID_EP,
            USB_IN_ENABLED|USB_OUT_ENABLED|USB_HANDSHAKE_ENABLED|USB_DISALLOW_SETUP );

    // re-arm the OUT endpoint for the next packet
    USBOutHandle = (volatile USB_HANDLE)HIDRxPacket( 
            CUSTOM_DEVICE_HID_EP,
            (uint8_t*)&rxDataBuffer,
            BUF_SIZE );
    
    // setup real time clock
    RCFGCAL = 0;            // this does most everything we want
    RCFGCALbits.RTCWREN;    // needed to allow writing to RTCVAL
    RCFGCALbits.RTCEN;      // start RTCC
    
    // initialize timer for ms timing operations
    T3CON = 0x0000;         // this does most everything we want
    T3CONbits.TCKPS = 3;    // 1:256 pre-scalar (w/FCY=32e6, each tick is 0.016ms)
    TMR3 = 0;               // zero out register
    T3CONbits.TON = 1;      // turn timer on, no need to have it off (except power)
    
    // pre-calculate multiplier to be applied to timer register (w/ 256 prescaler)
    // accuracy 4.86%
    msTimerMultiplier = 1 / ( ((float)(FCY))/256 ) * 1000;
    
    return;
}

void doUSBComm( void ) {
    
    // read rx buffer (if any) which will also prep our tx buffer
    if ( readUSB() ) {
        
        // then send our prepped tx buffer
        sendUSB();
        
        // re-arm the OUT endpoint to ready ourselves for the next OUT packet 
        // from the host
        USBOutHandle = HIDRxPacket(
            CUSTOM_DEVICE_HID_EP,
            (uint8_t*)&rxDataBuffer,
            BUF_SIZE );
        
     }    
    
    return;
}

void sendUSB( void ) {
    
    // check to make sure the endpoint/buffer is free before we modify the contents
    if ( !HIDTxHandleBusy(USBInHandle) ) {
        
        // Prepare the USB module to send the data packet to the host
        txIdx = 0;
        USBInHandle = HIDTxPacket(
            CUSTOM_DEVICE_HID_EP, 
            (uint8_t*)&txDataBuffer[0],
            BUF_SIZE );
        
    }
    
    return;
}

void _insertTxBufUnsigned( unsigned char data ) {
    
    // place data in byte chunks
    txDataBuffer[txIdx++] = data;
    
    return;
}

void insertTxBufUnsignedChar( unsigned char data ) {
    
    // place data in byte chunks
    _insertTxBufUnsigned( data );
    
    return;
}

void insertTxBufUnsignedInt( unsigned int data ) {
    
    char idx;
    
    // place data in byte chunks
    for ( idx=8; idx>=0; idx-=8 )
        _insertTxBufUnsigned( (unsigned char)(data>>idx) );
    
    return;
}

void insertTxBufUnsignedLong( unsigned long data ) {
    
    char idx;
    
    // place data in byte chunks
    for ( idx=24; idx>=0; idx-=8 )
        _insertTxBufUnsigned( (unsigned char)(data>>idx) );
    
    return;
}

unsigned char readUSB( void ) {
    
    unsigned char rxDataCmd;    
    unsigned int idx;
    
    // check if we have an OUT packet from host
    if ( !HIDRxHandleBusy(USBOutHandle) ) {
        
        // empty out tx buffer
        txIdx = 0;
        memset( &txDataBuffer[0], 0, sizeof(txDataBuffer) );
        
        // we got a packet, check what the hosts wants us to do and do it,
        // the packet contains a command id at the beginning followed by one or
        // more data byte that is used for certain commands, so let's look at
        // these to decide what to do
        idx = 0;
        while ( idx<(BUF_SIZE) ) {
            
            // get next address to figure out what we should do
            rxDataCmd = (unsigned char)(rxDataBuffer[idx++]);
            
            // if we ran out of addresses, exit
            if ( rxDataCmd == 0 )
                break;            
            
            if ( rxDataCmd == CMD_GET_TOBJ1.address ) {
                // send object temperature measurement to host
                
                // echo back command id to host along with temp, servo, and time data
                insertTxBufUnsignedChar( rxDataCmd );
                insertTxBufUnsignedInt( getTOBJ1() );
                insertTxBufUnsignedInt( 0 );
                insertTxBufUnsignedLong( (unsigned long)(currentTime) );
                insertTxBufUnsignedInt( (unsigned int)( ((float)(TMR3)) * msTimerMultiplier) );
                
                // increment counted base on size of packet
                //idx += (CMD_GET_TOBJ1.rxBytes+1);
            /*
            } else if ( rxDataCmd ==  CMD_GET_TOBJ2.address ) {
                // send object temperature measurement to host

                // echo back command id to host along with temp data
                insertTxBuf( idx, rxDataCmd, getTOBJ2() );

            } else if ( rxDataCmd == CMD_GET_TAMB.address ) {
                // send ambient temperature measurement to host

                // echo back command id to host along with temp data
                insertTxBuf( idx, rxDataCmd, getTAMB() );

            } else if ( rxDataCmd == CMD_DELAY_US.address ) {
                // pause a number of micro-seconds

                // use delay function to pause
                delay_us( rxDataOne );

                // echo back command id to host
                insertTxBuf( idx, rxDataCmd, rxDataOne );

            } else if ( rxDataCmd == CMD_DELAY_MS.address ) {
                // pause a number of milli-seconds

                // use delay function to pause
                delay_ms( rxDataOne );

                // echo back command id to host
                insertTxBuf( idx, rxDataCmd, rxDataOne );
                        */
            
            } else if ( rxDataCmd == CMD_GO_FLAG.address ) {
                // flag that this command packet is a pre-defined macro
                
                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );
                
            } else if ( rxDataCmd == CMD_GET_TIME.address ) {
                // retrieve timestamp (down to the ms)
                
                // echo back command id to host along with current time (and ms)
                insertTxBufUnsignedChar( rxDataCmd );
                insertTxBufUnsignedLong( (unsigned long)(currentTime) );
                insertTxBufUnsignedInt( (unsigned int)( ((float)(TMR3)) * msTimerMultiplier) );
                
            } else if ( rxDataCmd == CMD_SET_TIME.address ) {
                // set current time

                // set new unix time based on received data
                currentTime = (time_t)( 
                        (rxDataBuffer[idx++]<<24) |
                        (rxDataBuffer[idx++]<<16) |
                        (rxDataBuffer[idx++]<<8) |
                        (rxDataBuffer[idx++]<<0) );
                
                // zero out ms register
                TMR3 = 0;

                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );
                
            } else if ( rxDataCmd == CMD_LSR_SET.address ) {
                // set laser to specific value

                // set laser to a certain power level
                setLaserPower( rxDataBuffer[idx++]<<0 );

                // echo back command id to host along with laser power data
                insertTxBufUnsignedChar( rxDataCmd );

            } else if ( rxDataCmd == CMD_LSR_TOG.address ) {
                // set laser on/off

                // set laser to a certain power level
                if ( getLaserPower() )
                    setLaserPower( 0 );
                else
                    setLaserPower( 10 );

                // echo back command id to host along with laser power data
                insertTxBufUnsignedChar( rxDataCmd );
                insertTxBufUnsignedChar( getLaserPower() );
                
            /*
                //struct tm * pT;
                //char* asc;

                //time_raw_format = 1473370923;
                //ptr_time = localtime(&time_raw_format);
                //asc = asctime(ptr_time);

                // echo back command id to host
                insertTxBuf( idx, rxDataCmd, 0 );
            */
            
            } else if ( rxDataCmd == CMD_SET_PAN.address ) {
                // set gimbal pan angle

                // look at the data received to determine and the gimbal angle
                // to set, the first data byte is the msb of the angle and the 
                // second data byte is the lsb
                // this will be sent as an whole number, this number has a theoretical
                // maximum value of (2^16-1), divide this number by 360 to the angle
                // angular resolution = 360 / 2^16 = 0.0054931640625deg
                setGimbalPan( ((float)(unsigned int)( (rxDataBuffer[idx++]<<8) | (rxDataBuffer[(idx++)+1]) )) * 360 / ((float)(unsigned int)((1<<16)-1)) );
                
                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );
                
            } else if ( rxDataCmd == CMD_SET_TILT.address ) {
                // set gimbal pan angle

                // look at the data received to determine and the gimbal angle
                // to set, the first data byte is the msb of the angle and the 
                // second data byte is the lsb
                // this will be sent as an whole number, this number has a theoretical
                // maximum value of (2^16-1), divide this number by 360 to the angle
                // angular resolution = 360 / 2^16 = 0.0054931640625deg
                setGimbalTilt( ((float)(unsigned int)( (rxDataBuffer[idx++]<<8) | (rxDataBuffer[(idx++)+1]) )) * 360 / ((float)(unsigned int)((1<<16)-1)) );
                
                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );
                
            } else if ( rxDataCmd == CMD_LSR_SET.address ) {
                // set laser to specific value

                // set laser to a certain power level
                setLaserPower( rxDataBuffer[idx++] );

                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );

            } else if ( rxDataCmd == CMD_BEEP.address ) {
                // beep, pretty simple

                // if no data is specified, beep once
                if ( rxDataBuffer[idx] ) {
                    beep();
                    idx++;
                } else {
                    beepMulti( rxDataBuffer[idx++] );
                }
                
                // echo back command id to host
                insertTxBufUnsignedChar( rxDataCmd );
            
            /*
                        
            } else if ( rxDataCmd == CMD_BEEP_LONG.address ) {
                // long beep, also pretty simple

                // if no data is specified, beep once
                if ( rxDataOne == 0 )
                    beepLong();
                else
                    beepLongMulti( rxDataOne );

                // echo back command id to host
                insertTxBuf( idx, rxDataCmd, 0 );
                
            } else if ( rxDataCmd == CMD_BEEP_BYTE.address ) {
                // beep the byte pattern given

                // simply beep that byte given (if empty will beep all zeros,
                // aka not beep at all)
                beepByte( rxDataOne );

                // echo back command id to host
                insertTxBuf( idx, rxDataCmd, 0 );
            */
            
            } else if ( rxDataCmd == CMD_LED_TOG.address ) {
                // toggle debug led on and off

                // toggle output latch of debug led
                mDEBUG_LED_TOGGLE();
                
                // echo back command id to host along with LED state
                insertTxBufUnsignedChar( rxDataCmd );
                insertTxBufUnsignedChar( (unsigned char)(DEBUG_LED_LAT&DEBUG_LED_MASK) );
                
            /*
            } else if ( rxDataCmd == CMD_LED_SET.address ) {
                // set debug led to specific value

                // set the debug led on or off depending on input
                if ( rxDataCmd == 0 ) {
                    mDEBUG_LED_ON();
                } else {
                    mDEBUG_LED_OFF();
                }

                // echo back command id to host along with current led latch state
                insertTxBuf( idx, rxDataCmd, (unsigned char)(DEBUG_LED_LAT&DEBUG_LED_MASK) );
              */   
            } else {
                // we didn't get anything corresponding to our command ids...
                // do nothing
                
                // beep b/c we are confused
                beepLong();

                // echo back command id to host
                //insertTxBuf( idx, 0xEE, 0xEEEE );
                
             
            }
            
        }
        
        return 1;
    }
    
    return 0;
}