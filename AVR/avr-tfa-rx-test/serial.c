//-----------------------------------------------------------------------------
// Part of simple interface for radio sensors TFA Dostmann 30.3215.02.
// This module contains UART receiver ISR and SCPI command decoder.
// 
// UART data are received to ring buffer in ISR. 
// serial_decode() checks and disects commands separated by LF or semicolon.
// Transmission is not in ISR. 
// serial_error() function can hold SCPI style error string. Note it is 
// using malloc() to hold eventual error message, so heap should not be used
// anywhere else in the programm simultaneously.
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#include <avr/pgmspace.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay_basic.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "serial.h"

// rx data buffer
char rxd[RX_BUF_SZ];
// rx data position
volatile char *rxd_ptr;
volatile char *rxd_read;
// flags
volatile int8_t rxd_stat;

//----------------------------------------------------------------------------------
// UART STUFF
//----------------------------------------------------------------------------------

void inline serial_fifo_wrap(char *buf,char **addr)
{
	if(*addr >= &buf[RX_BUF_SZ])
		*addr = &buf[0]; // wrap around buffer
}

// USART ISR
ISR(USART0_RX_vect)
{
	// reenable ISR, but it is a bit risky if not processed before next byte
	//sei();

	char *ptr = (char*)rxd_ptr; // work with local copy - faster
	
	// read byte
	char dbyte = UDR0;
	
	// store data byte
	*ptr++ = dbyte;
	serial_fifo_wrap(rxd,&ptr);
	*ptr = '\0';
		
	// detect command end
	if(dbyte == '\n' || dbyte == ';')
		rxd_stat++;

	rxd_ptr = ptr; // store back local write pointer
}

// init USART
void serial_init(void)
{
	// init RX/TX
	UCSR0A = (1<<U2X0);
	UCSR0B = (1<<RXCIE0) | (1<<RXEN0) | (1<<TXEN0);
	UCSR0C = (0<<UMSEL00) | (0<<UPM00) | (0<<USBS0) | (3<<UCSZ00);
	UBRR0 = (uint16_t)((F_CPU/(USART_BAUDRATE*8ul)) - 1);

	rxd[0] = '\0';
	rxd_ptr = &rxd[0]; // write pointer
	rxd_read = &rxd[0]; // read pointer
	rxd_stat = 0; // no command yet
}

// decode command, supports following format:
//  "my:command:or:whatever[<space(s)>parameter]"
uint8_t serial_decode(char *cbuf,char **par)
{
	// check command completness
	int8_t count = 0;
	int8_t stat;
	ATOMIC_BLOCK(ATOMIC_FORCEON)
	{
		stat = rxd_stat;
	}
	if(stat < 1)
		return(0); // no command yet - get out
	
	// we have command in buffer - quickly copy it to secondary buffer
	
	// command buffer pointer
	char *com = cbuf;
	*com = '\0';

	// rx buffer read pointer
	char *ptr = (char*)rxd_read;

	// no parameter detected yet
	*par = NULL;

	// end of command reached?
	uint8_t is_end = 0;

	while(1)
	{
		// get RX byte
		char db = *ptr;		

		if(db == ' ' && !*par)
		{
			// parameter separator
			*com++ = '\0';		

			// return parameter poiner
			*par = com;
			
			// skip parameter separator(s)
			while(*ptr == ' ')
			{
				ptr++;
				serial_fifo_wrap(rxd,&ptr);
			}
			
			continue;			
		}
		else if(db == ';' || db == '\n' || db == '\r')
		{
			// update remainig events count
			if(db != '\r')
				count++;
			
			// end of command
			if(!is_end)
				*com++ = '\0';
			
			// end of command reached, but check if there is additional rubbish after
			is_end = 1;			
		}
		else if(is_end)
		{
			// end of command reached
			rxd_read = ptr; // store next command read position
			break;
		}
		else
		{
			// copy command data
			*com++ = db;
		}
		
		// move to next RX byte
		ptr++;
		serial_fifo_wrap(rxd,&ptr);
	}

	// update new command events count
	ATOMIC_BLOCK(ATOMIC_FORCEON)
	{
		rxd_stat -= count;
	}
	
	return(cbuf[0] != '\0'); // command detected
}

// send byte
void serial_tx_byte(uint8_t byte)
{
	loop_until_bit_is_set(UCSR0A,UDRE0);
	UDR0 = byte;
}

// wait TX done
void serial_tx_wait(void)
{
	loop_until_bit_is_set(UCSR0A,TXC0);
}

// send string from progmem
void serial_tx_cstr(const char *str)
{
	char byte;
	while((byte = pgm_read_byte(str++)) != '\0')
	{
		sbi(UCSR0A,TXC0);
		serial_tx_byte(byte);
	}
	loop_until_bit_is_set(UCSR0A,TXC0);		
}

// send string from progmem
void serial_tx_str(char *str)
{
	char byte;
	while((byte = *str++) != '\0')
		serial_tx_byte(byte);
}




// --- SCPI error generator ---

// err: error code; info: optional error message; mode: flags
void serial_error(int16_t err,const char *info,uint8_t mode)
{
	static uint16_t err_mem = 0;
	static char *info_mem = NULL;
	static uint8_t is_malloc = 0;
	
	// remember last error
	if(mode&SCPI_ERR_STORE)
	{
		// loose old info buffer
		if(is_malloc && info_mem)
			free((void*)info_mem);
		info_mem = NULL;

		err_mem = err;
		if(mode & SCPI_ERR_STR)
		{
			// RAM string mode						
			info_mem = (char*)malloc(strlen(info)+1);
			strcpy(info_mem,info);
			is_malloc = 1;
		}
		else
		{
			// PSTR mode
			info_mem = (char*)info;
			is_malloc = 0;
		}
	}

	if(mode&SCPI_ERR_SEND)
	{
		switch(err_mem)
		{
			case SCPI_ERR_undefinedHeader:
				serial_tx_cstr(PSTR("-113, Undefined command header.")); break;
			case SCPI_ERR_wrongParamType:
				serial_tx_cstr(PSTR("-104, Wrong parameter type or value.")); break;
			case SCPI_ERR_tooFewParameters:
				serial_tx_cstr(PSTR("-109, Missing parameters.")); break;
			case SCPI_ERR_std_mediaProtected:
				serial_tx_cstr(PSTR("-258, EEPROM write protected.")); break;
			case SCPI_ERR_noError:
				serial_tx_cstr(PSTR("0, No error.")); break;
			default:
				break;

		}
		if(info_mem)
		{
			serial_tx_byte(' ');
			if(is_malloc)
				serial_tx_str(info_mem);
			else
				serial_tx_cstr(info_mem);
		}
		serial_tx_cstr(PSTR("\n"));

		// loose old info buffer
		if(is_malloc && info_mem)
			free((void*)info_mem);
		info_mem = NULL;
		err_mem = 0;		
	}
	
}
