//-----------------------------------------------------------------------------
// Part of simple interface for radio sensors TFA Dostmann 30.3215.02.
// See tfa.c and main.c for more details.
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#ifndef TFA_H_
#define TFA_H_

// TFA sampling tick:
#define TFA_TICK 50e-6 /* desired TICK rate [s] */
#define TFA_TIMER ((int)(TFA_TICK*F_CPU/8.0) - 1) /* timer divisor for the tick */
#define TFA_TICK_REAL (8.0*((int)TFA_TIMER+1)/F_CPU) /* actual tick rate after timer divisor rounding [s] */

#define LED_DELAY (0.25/TFA_TICK_REAL) /* LED indication duration in ticks */

// TFA 30.3215.02 timing:
#define TFA_T_SHORT 1.8e-3 /* short-low pulse (low state) [s] */
#define TFA_T_LONG 3.6e-3 /* long-low pulse (high state) [s] */
#define TFA_T_MID (0.5*(TFA_T_SHORT + TFA_T_LONG)) /* decision rule between low/high pulse [s] */
#define TFA_T_START (5e-3) /* start-low pulse decision rule [s] */
#define TFA_T_STOP (0.75*TFA_T_SHORT) /* stop-low pulse decision rule [s] */
#define TFA_T_GAP (10e-3) /* gap to signalize end of transmission [s] */
#define TFA_T_GLITCH (0.2e-3) /* glitch limit to reject pulse [s] */
// TFA decision macros
#define TFA_IS_GLITCH(ticks) (ticks < TFA_T_GLITCH/TFA_TICK_REAL) /* is pulse glitch? */
#define TFA_IS_STOP(ticks) (ticks < TFA_T_STOP/TFA_TICK_REAL) /* is pulse start bit? */
#define TFA_IS_LOW(ticks) (ticks < TFA_T_MID/TFA_TICK_REAL) /* is pulse low state? */
#define TFA_IS_HIGH(ticks) (ticks >= TFA_T_MID/TFA_TICK_REAL) /* is pulse high state? */
#define TFA_IS_START(ticks) (ticks > TFA_T_START/TFA_TICK_REAL) /* is pulse start bit? */
#define TFA_IS_GAP(ticks) (ticks > TFA_T_GAP/TFA_TICK_REAL) /* is pulse end of transmission? */

// TFA 30.3215.02 packet setup
#define TFA_BITS 36 /* single packet bits count */
#define TFA_BUF_BYTES 5 /* single packet buffer bytes */
#define TFA_PACKETS 7 /* TFA packets count */
#define TFA_TYPE 0x90 /* TFA 30.3215.02 type id (probably) */

#define TFA_NEW_PACKETS (1<<0) /* new packets received */
#define TFA_NEW_PACKET (1<<1) /* new processed packet available */
typedef struct{
	uint8_t data[TFA_PACKETS][TFA_BUF_BYTES];
	uint8_t packets;
	uint8_t packet[TFA_BUF_BYTES];
	uint8_t flags;
}TTFA;

#define SENSOR_CHANNELS 3 /* recognized sensor channels */

// decoded sensor data
//  note: make these flags not colliding with TTFA flags!
#define TFA_SYNC (1<<6) /* sync button pressed flag */
#define TFA_LOW_BATT (1<<7) /* new packets received */

#define SENSOR_IS_SYNC(sens_flags) !!(sens_flags & TFA_SYNC)
#define SENSOR_IS_LOW_BATT(sens_flags) !!(sens_flags & TFA_LOW_BATT)

typedef struct{
	uint8_t id;
	uint8_t channel;
	float temp;
	uint8_t rh;	
	uint8_t type;
	uint8_t flags;
}TSensor;


// --- functions:
void tfa_init(TTFA *tfa);
uint8_t tfa_proc_packets(TTFA *tfa);
uint8_t tfa_parse(TTFA *tfa, TSensor *sensor);



#endif