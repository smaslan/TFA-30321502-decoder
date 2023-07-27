//-----------------------------------------------------------------------------
// Part of simple interface for radio sensors TFA Dostmann 30.3215.02.
// This module contains UART receiver ISR and SCPI command decoder.
// See serial.c and main.c for details.
//
// (c) 2023, Stanislav Maslan, s.maslan@seznam.cz
// url: https://github.com/smaslan/TFA-30321502-decoder
// V1.0, 2023-07-27, initial version
//
// The code and all its part are distributed under MIT license
// https://opensource.org/licenses/MIT.
//-----------------------------------------------------------------------------

#ifndef SERIAL_H
#define SERIAL_H

// --- USART config ---
#define USART_BAUDRATE 19200 /* baud rate (do not set too high!) */
#define RX_BUF_SZ 128 /* receive buffer size (max 255!) */
#define RX_DONE 0 /* command received flag */


// --- SCPI errors ---
// SCPI error info buffer size [B]
#define SCPI_ERR_MAXBUF 64

// SCPI error flags
#define SCPI_ERR_STORE 1 /* store error message to buffer */
#define SCPI_ERR_SEND 2 /* send error from internal buffer */
#define SCPI_ERR_PSTR 0 /* info is PSTR */
#define SCPI_ERR_STR 4 /* info is RAM string */

// SCPI errors
#define SCPI_ERR_noError 0l
#define SCPI_ERR_undefinedHeader -113l
#define SCPI_ERR_wrongParamType -104l
#define SCPI_ERR_tooFewParameters -109l
#define SCPI_ERR_std_mediaProtected -258l


// --- prototypes ---
void serial_init(void);
uint8_t serial_decode(char *cbuf,char **par);
void serial_tx_byte(uint8_t byte);
void serial_tx_cstr(const char *str);
void serial_tx_str(char *str);
void serial_error(int16_t err,const char *info,uint8_t mode);



#endif
