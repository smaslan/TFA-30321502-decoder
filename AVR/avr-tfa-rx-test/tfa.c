//-----------------------------------------------------------------------------
// Part of simple interface for radio sensors TFA Dostmann 30.3215.02.
// This module contains decoder functions for sensor type 30.3215.02.
//
// Sensor is using following format:
// Every transmission of sensor consist of 7 repetitions of the same packet.
// Data coding is PPM (pulse position modulation) driven by gap (low) length.
// Start bit is long gap (~8ms), stop bit is short gap (~0.5ms).
// High bit is long gap (~3.6ms), low bit is short gap (~1.8ms).
// Pulse width is approx 0.5ms. It may vary on receiver and signal strength.
// There is no CRC. It can be replaced by comparing the 7 repetitions
// and selecting most common packet data (done in this implementation).
//
// Packet has 36 bits. Bit 0 if first received, the content is:
//   bits[7..0]   = sensor type ID (0x90)?
//   bits[11..8]  = random ID (generated when battery replaced)?
//   bits[12]    = battery low flag (1=low, 0=good)
//   bits[13]    = sync button pressed (1=sync report, 0=self reporting)
//   bits[15:14] = channel ID as set on switch (0=chn1, 1=chn2, ...)
//   bits[27:16] = 2's complement temperature [10*deg C] (237=23.7degC)
//   bits[35:28] = relative humidity [%]
//
// This implementation is using timer with constant sampling rate 
// of 50us (should work even slower) for sampling ASK radio module data
// and low-pulse duration measurement. The same 50us ISR can be used for
// other stuff like buttons or encoders decoding. It stores the received 
// bits to local array. After successful reception it returns the data
// to working buffer which is post processed in main program loop to not
// delay the ISR. Note the receiver places bits to buffer in reverse order
// to make parsing the data easier.
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <string.h>

#include "tfa.h"
#include "main.h"

// received data pointer
TTFA *p_tfa;

// initialize TFA decoder
void tfa_init(TTFA *tfa)
{
	// RX module input (no pullup)
	cbi(ARX_DDR,ARX);
	cbi(ARX_PORT,ARX);

	// data indicator LED
	sbi(LED_PACKET_DDR,LED_PACKET);
	
	// timer 0 tick (controls)
	TCCR0A = (2<<WGM00);
	TCCR0B = (2<<CS00); // XCLK/8
	OCR0A = (uint8_t)TFA_TIMER;
	TIMSK0 |= (1<<OCIE0A);

	// reset TFA receiver
	p_tfa = tfa;
	p_tfa->flags = 0;
}

// TFA decoder tick ISR ---
ISR(TIMER0_COMPA_vect)
{
	static uint16_t led_delay = 0;
	static uint8_t tfa_old = 0x00;	

	// sampling radio RX data
	uint8_t tfa_state = ARX_PIN&(1<<ARX);

	// detect changes
	uint8_t tfa_edge = tfa_state^tfa_old;
	uint8_t tfa_fall = tfa_edge&tfa_old;
	uint8_t tfa_rise = tfa_edge&tfa_state;

	// store old state
	tfa_old = tfa_state;

	// low-pulse duration timer
	static uint8_t tfa_timer = 0;

	static uint8_t tfa_buf[TFA_PACKETS][TFA_BUF_BYTES];
	static int8_t tfa_buf_bit = 0;
	static uint8_t tfa_buf_packet = 0;

	if(tfa_fall)
	{
		// pulse start: reset pulse timer
		tfa_timer = 0;
	}
	else if(tfa_rise)
	{
		// pulse end: decode
		if(TFA_IS_GLITCH(tfa_timer))
		{
			// glitch pulse - reject
			tfa_buf_bit = -1;
		}
		else if(TFA_IS_STOP(tfa_timer))
		{			
			// stop bit - packet end			
			if(tfa_buf_bit == 0)
			{								
				// full packet received
				tfa_buf_bit--;
				if(tfa_buf_packet < TFA_PACKETS+1)
					tfa_buf_packet++;
			}			
		}
		else if(TFA_IS_GAP(tfa_timer))
		{
			// end of transmission: copy data to destination buffer (processing is offloaded to main loop to save ISR time)
			if(tfa_buf_packet >= 3 && tfa_buf_packet <= TFA_PACKETS)
			{				
				//serial_tx_byte(tfa_buf_packet);
				memcpy((void*)p_tfa->data,(void*)tfa_buf,TFA_PACKETS*TFA_BUF_BYTES);
				p_tfa->packets = tfa_buf_packet;
				p_tfa->flags |= TFA_NEW_PACKETS;				
				// new packet LED pulse
				led_delay = 0;
				sbi(LED_PACKET_PORT,LED_PACKET);
			}
			// restart receiver
			tfa_buf_packet = 0;			
		}
		else if(TFA_IS_START(tfa_timer))
		{									
			// start bit		
			tfa_buf_bit = TFA_BITS;
			if(tfa_buf_packet < TFA_PACKETS)
				tfa_buf[tfa_buf_packet][TFA_BUF_BYTES-1] = 0x00; // clear last unfull byte of packet
		}		
		else
		{
			// data bit - place to buffer			
			if(tfa_buf_bit > 0 && tfa_buf_packet < TFA_PACKETS)
			{				
				tfa_buf_bit--;
				uint8_t data = TFA_IS_HIGH(tfa_timer);
				uint8_t bit = 1<<(tfa_buf_bit&0x07u);
				uint8_t *byte = &tfa_buf[tfa_buf_packet][tfa_buf_bit>>3];
				*byte = *byte & ~bit;
				if(data)				
					*byte |= bit;
			}
			else if(tfa_buf_bit >= 0)
				tfa_buf_bit--;
		}

	}		
	if(tfa_timer < 255)
		tfa_timer++;

	// delayed LED indicator
	led_delay++;
	if(led_delay >= LED_DELAY)
		cbi(LED_PACKET_PORT,LED_PACKET);
}

// process received packets to final data
// note: this must be called outside ISR to not block it as it is time consuming
uint8_t tfa_proc_packets(TTFA *tfa)
{
	if(!(tfa->flags & TFA_NEW_PACKETS))
		return(0);
	// some packet available
	
	// make atomic copy (to not disturb and be disturbed by receiver ISR)
	uint8_t buf[TFA_PACKETS][TFA_BUF_BYTES];
	uint8_t packets;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		memcpy((void*)buf,(void*)tfa->data,TFA_PACKETS*TFA_BUF_BYTES);	
		packets = tfa->packets;
		tfa->flags &= ~TFA_NEW_PACKETS;
	}

	// go through all received packets and find the one with most repetitions
	int8_t counts[TFA_PACKETS];
	memset((void*)counts,0,TFA_PACKETS);
	int8_t maxv[2] = {0,0};
	int8_t maxid = 0;	
	for(uint8_t m = 0;m < packets;m++)
	{
		if(counts[m] != 0)
			continue;
		for(uint8_t n = 0;n < packets;n++)
		{
			if(counts[n] == 0 && !memcmp((void*)&buf[m][0],(void*)&buf[n][0],TFA_BUF_BYTES))
			{
				counts[m]++;
				if(counts[m] > maxv[0])
				{
					// detect packet with maximum repetitions
					maxv[1] = maxv[0];
					maxv[0] = counts[m];
					maxid = m;
				}
				if(counts[n])
					counts[n] = -1;
			}
		}
	}
	if(maxv[0] == maxv[1])
	{
		// cannot decide most common packet data
		return(0);
	}
	// finally select the correct packet
	memcpy((void*)tfa->packet,(void*)&buf[maxid][0],TFA_BUF_BYTES);

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		tfa->flags |= TFA_NEW_PACKET;
	}
	return(1);
}

// parse packet data to sensor struct
uint8_t tfa_parse(TTFA *tfa, TSensor *sensor)
{
	sensor->rh = tfa->packet[0];	
	uint16_t temp = *((uint16_t*)&tfa->packet[1]) & 0x0FFFu;
	if(temp&0x0800u)
		temp |= 0xF000u;
	sensor->temp = 0.1*(float)*(int16_t*)&temp;
	sensor->channel = 1 + ((tfa->packet[2]>>4)&0x03);
	sensor->id = tfa->packet[3] & 0x0F;
	sensor->type = (uint8_t)(*((uint16_t*)&tfa->packet[3]) >> 4);
	sensor->flags = TFA_NEW_PACKET | (tfa->packet[2] & (TFA_LOW_BATT | TFA_SYNC));
	return(sensor->type == TFA_TYPE);
}

