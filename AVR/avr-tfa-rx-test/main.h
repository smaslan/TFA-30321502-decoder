//-----------------------------------------------------------------------------
// Part of simple interface for radio sensors TFA Dostmann 30.3215.02.
// See main.c for more details.
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#ifndef MAIN_H_
#define MAIN_H_

// general macros
#define sbi(port,pin) {port|=(1<<pin);}
#define cbi(port,pin) {port&=~(1<<pin);}
#define min(a,b) ((a<b)?(a):(b))
#define max(a,b) ((a>b)?(a):(b))
#define bcopy(dreg,dpin,srreg,srpin) if(bit_is_set(srreg,srpin)){sbi(dreg,dpin);}else{cbi(dreg,dpin);}
#define bcopy_v(dreg,dpin,srreg,srpin) if((srreg)&(1<<srpin)){sbi(dreg,dpin);}else{cbi(dreg,dpin);}
#define low(x) ((x) & 0xFFu)
#define high(x) (((x)>>8) & 0xFFu)


// system control
#define SYST_TALK (1<<0) /* auto talk mode when packet received? */
#define SYST_HEAD (1<<1) /* show headers when reporting packet data? */

typedef struct{
	uint16_t packets; /* received packets */
	uint8_t flags; /* control flags */
}TSystem;


// data input from RX module
#define ARX_PORT PORTD
#define ARX_DDR DDRD
#define ARX_PIN PIND
#define ARX PD2

// TFA receiver LED
#define LED_PACKET_PORT PORTD
#define LED_PACKET_DDR DDRD
#define LED_PACKET PD3

// unread data LED
#define LED_UNREAD_PORT PORTD
#define LED_UNREAD_DDR DDRD
#define LED_UNREAD PD4


#endif