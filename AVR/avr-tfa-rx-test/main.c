//-----------------------------------------------------------------------------
// Simple interface for radio sensors TFA Dostmann 30.3215.02.
// Receives packets from ASK radio module (tested with Aurel AC-RX2/CS).
// Communicates results via UART using standard SCPI style commands.
// Developed for 8-bit AVR ATmega644, but will work on any AVR.
// This demo uses internal clock of 8MHz, which limits selectable baudrates.
//
// Communication:
//   Setup: 19200bd, 8bit, no parity, no flow control, 1stop
//   SCPI style commands are terminated by LF (0x0A), can be chained by ';',
//   maximum command chain is 127 bytes. Answers are LF terminated.
//
//   Supported commands:
//     *IDN? - return identification string
//     *RST - reboot
//     SYST:ERR? - return last error if any
//     TFA:TALK <0|1> - disable/enable auto reporting of received sensor data
//     TFA:HEAD <0|1> - return data with text headers?
//     TFA:DATA? - return last sensor data for any channel
//     TFA:DATA:NEW? - check if there are new unread sensor data for any chn.
//     TFA:SYNC - start synchronization for all sensor channels
//     TFA:DATA? <1|2|3> - return last sensor data for given channel 1-3
//     TFA:DATA:NEW? <1|2|3> - check if there are new unread channel data
//     TFA:SYNC <1|2|3> - start synchronization for selected channel
//     TFA:COUNT? - get received sensor data count
//     TFA:COUNT:RESET - reset received sensor data count
//
//   Reporting format:
//     "id= 9, chn=2, t=23.7"C, rh=45%, batt=1, sync=0\n" with headers
//     "9, 2, 23.7, 45, 1, 0\n" without headers
//
//     id - random sensor 4-bit ID
//     chn - channel setup as on switch in sensor (1-3)
//     t - temperature in [degC]
//     rh - relative humidity [%]
//     batt - 1 of low battery
//     sync - 1 if sync button on sensor pressed, 0 for normal reporting
//
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay_basic.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "tfa.h"
#include "serial.h"

// --- jump to bootloader ---
#define boot_start(boot_addr) {goto *(const void* PROGMEM)boot_addr;}
//#define boot_start(boot_addr) asm volatile ("ijmp" ::"z" (boot_addr));


// print sensor data
void tfa_print_sensor(TSystem *syst, TSensor *sensor)
{
	char str[64];
	if(syst->flags & SYST_HEAD)
		sprintf_P(str,PSTR("id=%2u, chn=%u, t=%0.1f\"C, rh=%u%%, batt=%u, sync=%u\n"),sensor->id,sensor->channel,sensor->temp,sensor->rh,SENSOR_IS_LOW_BATT(sensor->flags),SENSOR_IS_SYNC(sensor->flags));
	else
		sprintf_P(str,PSTR("%2u, %u, %0.1f, %u, %u, %u\n"),sensor->id,sensor->channel,sensor->temp,sensor->rh,SENSOR_IS_LOW_BATT(sensor->flags),SENSOR_IS_SYNC(sensor->flags));
	serial_tx_str(str);
}


// --- MAIN ---
int main(void)
{    		
	// data indicator LED
	sbi(LED_UNREAD_DDR,LED_UNREAD);

	// sensor data buffer (holds received packet, needs to be atomically accessed)
	TTFA tfa;

	// TFA decoder initialization (uses timer 0)
	tfa_init(&tfa);

	// initialize UART and SCPI receiver
	serial_init();

	// system control&status
	TSystem syst = {0,SYST_TALK|SYST_HEAD};

	// sensor channels (holds last data for each channel)
	TSensor sensors[3];
	for(uint8_t k=0;k<SENSOR_CHANNELS;k++)
	{
		sensors[k].id = 0xFF; // reset channel ID (sync)
		sensors[k].flags = 0; // no data yet
	}
	
	// enable global IRQ
	sei();
		
			
    while(1)
	{		
		// --- SCPI command handlers:
		char str[32]; // response buffer
		char cmdbuf[RX_BUF_SZ]; // local command buffer
		char *par;
		if(serial_decode(cmdbuf,&par)) // check and eventual SCPI command presence
		{
			// yaha, some command present: decode

			if(!strcmp_P(cmdbuf,PSTR("TFA:TALK")))
			{
				// TFA:TALK <state> - enable or disable auto reporting of received packet {0,1}
				if(!par || *par < '0' || *par > '1')
				{
					serial_error(SCPI_ERR_wrongParamType,PSTR("TFA:TALK parameter must be 0 or 1."),SCPI_ERR_STORE|SCPI_ERR_PSTR);
					goto SCPI_error;
				}
				syst.flags &= ~SYST_TALK;
				syst.flags |= (*par - '0')*SYST_TALK;
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:HEAD")))
			{
				// TFA:HEAD <state> - enable or disable headers when reporting data {0,1}
				if(!par || *par < '0' || *par > '1')
				{
					serial_error(SCPI_ERR_wrongParamType,PSTR("TFA:HEAD parameter must be 0 or 1."),SCPI_ERR_STORE|SCPI_ERR_PSTR);
					goto SCPI_error;
				}
				syst.flags &= ~SYST_HEAD;
				syst.flags |= (*par - '0')*SYST_HEAD;
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:DATA:NEW?")))
			{
				// TFA:DATA:NEW? <channel> - new unread sensor data available? optional <channel> points to particular sensor channel
				uint8_t chn = 0;
				if(par)
				{
					chn = atoi(par);
					if(chn < 1 || chn > SENSOR_CHANNELS)
					{
						serial_error(SCPI_ERR_wrongParamType,PSTR("TFA:DATA:NEW? <channel> parameter must be 1 to 3 or empty."),SCPI_ERR_STORE|SCPI_ERR_PSTR);
						goto SCPI_error;
					}
				}
				
				uint8_t new;
				if(!chn)
					new = tfa.flags & TFA_NEW_PACKET;
				else
					new = sensors[chn-1].flags & TFA_NEW_PACKET;				
				if(new)
					serial_tx_cstr(PSTR("1\n"));
				else
					serial_tx_cstr(PSTR("0\n"));
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:DATA?")))
			{
				// TFA:DATA? <channel> - read last received sensor data (any sensor) or particular sensor <channel>
				uint8_t chn = 0;
				if(par)
				{
					chn = atoi(par);
					if(chn < 1 || chn > SENSOR_CHANNELS)
					{
						serial_error(SCPI_ERR_wrongParamType,PSTR("TFA:DATA? <channel> parameter must be 1 to 3 or empty."),SCPI_ERR_STORE|SCPI_ERR_PSTR);
						goto SCPI_error;
					}
				}				
				
				if(!chn)
				{
					// parse and print sensor data
					TSensor sensor;
					if(tfa_parse(&tfa,&sensor))
						tfa_print_sensor(&syst,&sensor);
					else
						serial_tx_cstr(PSTR("error parsing data: unknown sensor type?\n"));										
				}
				else
				{
					// print particular sensor channel
					tfa_print_sensor(&syst,&sensors[chn-1]);
					sensors[chn-1].flags &= ~TFA_NEW_PACKET;
				}
				// clear new data flag
				ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
				{
					tfa.flags &= ~TFA_NEW_PACKET;
				}
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:SYNC")))
			{
				// TFA:SYNC <channel> - start synchronization with sensor, optional <channel> points to particular channel
				uint8_t chn = 0;
				if(par)
				{
					chn = atoi(par);
					if(chn < 1 || chn > SENSOR_CHANNELS)
					{
						serial_error(SCPI_ERR_wrongParamType,PSTR("TFA:SYNC <channel> parameter must be 1 to 3 or empty."),SCPI_ERR_STORE|SCPI_ERR_PSTR);
						goto SCPI_error;
					}
				}
				
				if(!chn)				
				{
					// sync all channels
					for(uint8_t k=0;k<SENSOR_CHANNELS;k++)
						sensors[k].id = 0xFF;
				}
				else
				{
					// sync one channel
					sensors[chn-1].id = 0xFF;
				}				
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:COUNT?")))
			{
				// TFA:COUNT? - get received sensor data count
				if(par)
				{
					serial_error(SCPI_ERR_wrongParamType,PSTR("No parameters expected for TFA:COUNT?"),SCPI_ERR_STORE|SCPI_ERR_PSTR);
					goto SCPI_error;
				}
				sprintf(str,"%u\n",syst.packets);
				serial_tx_str(str);				
			}
			else if(!strcmp_P(cmdbuf,PSTR("TFA:COUNT:RESET")))
			{
				// TFA:COUNT:RESET - reset received sensor data count
				if(par)
				{
					serial_error(SCPI_ERR_wrongParamType,PSTR("No parameters expected for TFA:COUNT:RESET"),SCPI_ERR_STORE|SCPI_ERR_PSTR);
					goto SCPI_error;
				}
				syst.packets = 0;
			}
			else if(!strcmp_P(cmdbuf,PSTR("*IDN?")))
			{
				// "*IDN?" to return IDN string
				serial_tx_cstr(PSTR("TFA Dostmann 30.3215.02 radio interface by Stanislav Maslan, V1.0, " __DATE__ "\n"));
			}
			else if(!strcmp_P(cmdbuf,PSTR("*RST")))
			{
				// *RST - restarts controller
				cli();
				boot_start(0x0000ul);
				while(1);
			}
			else if(!strcmp_P(cmdbuf,PSTR("SYST:ERR?")))
			{
				// SYST:ERR? - return last error
				serial_error(SCPI_ERR_undefinedHeader,NULL,SCPI_ERR_SEND);
			}			
			else
			{
				// invalid
				serial_error(SCPI_ERR_undefinedHeader,cmdbuf,SCPI_ERR_STORE|SCPI_ERR_STR);
				goto SCPI_error;
			}

			// nasty error handler :)
			SCPI_error:;
		}

		// unread data LED
		if(tfa.flags & TFA_NEW_PACKET)
			sbi(LED_UNREAD_PORT,LED_UNREAD)
		else
			cbi(LED_UNREAD_PORT,LED_UNREAD)

		// --- offloaded received packets processing:
		if(tfa_proc_packets(&tfa))
		{			
			// new packet received: decode
			TSensor sensor;
			uint8_t type_ok = tfa_parse(&tfa,&sensor);			
			if(type_ok)
			{
				// sensor type matching
				
				// update statistics
				syst.packets++;

				if(sensor.channel > 0 && sensor.channel <= SENSOR_CHANNELS)
				{
					// copy new sensor data to channel if ID match or not yet assigned ID (sync mode)
					TSensor *dsens = &sensors[sensor.channel - 1];					
					if(dsens->id == 0xFF || dsens->id == sensor.id)
						memcpy((void*)dsens,(void*)&sensor,sizeof(TSensor));
				}

				if(syst.flags & SYST_TALK)
				{
					// talk mode: report any valid packet now and clear new data flag
					tfa_print_sensor(&syst,&sensor);
					ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
					{
						tfa.flags &= ~TFA_NEW_PACKET;
					}
				}
			}
		}
	}
}

