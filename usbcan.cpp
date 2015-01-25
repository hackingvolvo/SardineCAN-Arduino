/* Sardine CAN (Open Source J2534 device) - Arduino firmware - version 0.4 alpha
**
** Copyright (C) 2012 Olaf @ Hacking Volvo blog (hackingvolvo.blogspot.com)
** Author: Olaf <hackingvolvo@gmail.com>
** Most of the code here copied from "AVR based USB<>CAN adaptor" by Michael Wolf
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published
** by the Free Software Foundation, either version 3 of the License, or (at
** your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with this program; if not, <http://www.gnu.org/licenses/>.
**
*/
#include <avr/eeprom.h>

#include <TimerOne.h>
#include <arduino.h>

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include "sardine.h"
#include "usbcan.h"

//#define usb_putc(x) Serial.write(x)
//#define usb_puts(x) Serial.print(x)

// CAN tx message
//extern 
struct {
    uint8_t format;		// Extended/Standard Frame
    uint32_t id;		    // Frame ID
    uint8_t rtr;		    // RTR/Data Frame
    uint8_t len;		    // Data Length
    uint8_t data[8];		// Data Bytes
} CAN_tx_msg;			      // length 15 byte/each
// CAN rx message
//extern
struct {
    uint8_t format;		// Extended/Standard Frame
    uint32_t id;		    // Frame ID
    uint8_t rtr;		    // RTR/Data Frame
    uint8_t len;		    // Data Length
    uint8_t data[8];		// Data Bytes
} CAN_rx_msg;			      // length 15 byte/each

// CAN init values
//extern 
struct {
    uint8_t acr[4];
    uint8_t amr[4];
    uint8_t btr0;
    uint8_t btr1;
    uint8_t fixed_rate;
} CAN_init_val;


// time stamp counter 0-59999ms
static uint16_t timestamp;


// define EEPROM settings
__attribute__ ((section (".eeprom")))
     uint8_t serial[] = USBCAN_SERIAL;	// store device serial
__attribute__ ((section (".eeprom")))
     uint8_t ee_timestamp_status = 0;	// store time stamp OFF
  
// copy of ee_time_stamp_status in SRAM to prevent unnecessary EEPROM access
     uint8_t ram_timestamp_status;

// execute command received via USB
     uint8_t exec_usb_cmd (uint8_t * cmd_buf);

// convert 2 byte ASCII to one byte
     uint8_t ascii2byte (uint8_t * val);
     
void usb_putc (uint8_t tx_byte)
{
  printf("%c",tx_byte);
}

void usb_puts (uint8_t * tx_string)
{
  printf("%s",tx_string);
}


/*
**---------------------------------------------------------------------------
**
** Abstract: USB one byte as 2 ASCII chars
**
**
** Parameters: character to send via USB
**
**
** Returns: none
**
**
**---------------------------------------------------------------------------
*/
void
usb_byte2ascii (uint8_t tx_byte)
{
    // send high nibble
    usb_putc (((tx_byte >> 4) <
	       10) ? ((tx_byte >> 4) & 0x0f) + 48 : ((tx_byte >> 4) & 0x0f) +
	      55);
    // send low nibble
    usb_putc (((tx_byte & 0x0f) <
	       10) ? (tx_byte & 0x0f) + 48 : (tx_byte & 0x0f) + 55);
}


/*
**---------------------------------------------------------------------------
**
** Abstract: Timer0 output capture interrupt handler
**           called in 1ms intervall
**
**
** Parameters: none
**
**
** Returns: time stamp counter 0-59999ms
**
**---------------------------------------------------------------------------
*/
void IncreaseTimestamp()
{
    timestamp++;
    if (timestamp > 59999)
        timestamp = 0;
}
     
     
			
/*
**---------------------------------------------------------------------------
**
** Abstract: Transmit CAN message
**
**
** Parameters: none, but message must be stored in CAN_tx_msg
**
** Returns: status of command execution
**          CR = OK
**          ERROR = Error
**
**---------------------------------------------------------------------------
*/
uint8_t
transmit_CAN (void)
{
    tCAN msg;
    
    // FIXME: we are now copying from UsbCAN struct to MCP2515 driver struct. Should clean up this mess and use the destination struct from the begininning

    msg.header.length = CAN_tx_msg.len & 0x0F;

    // check for remote transmission request
    if (CAN_tx_msg.rtr)
        msg.header.rtr = 1; //temp_frame_info |= _BV (RTR_Bit);
    else
        msg.header.rtr = 0;

    // check for extented frame usage
    if (CAN_tx_msg.format)
      msg.header.eid = 1; //temp_frame_info |= _BV (FF_Bit);
    else
      msg.header.eid = 0;

    msg.id = CAN_tx_msg.id;
    int i=0;
    for (;i<8;i++)
      msg.data[i] = CAN_tx_msg.data[i];
      
  if (send_CAN_msg(&msg)==0)
    return ERROR;
  return CR;
}


/*
**---------------------------------------------------------------------------
**
** Abstract: Execution of command received via USB
**
**
** Parameters: Command Buffer
**
**
** Returns: status of command execution
**          CR = OK
**          ERROR = Error
**
**---------------------------------------------------------------------------
*/
uint8_t
exec_usb_cmd (uint8_t * cmd_buf)
{
    uint8_t cmd_len = strlen ((char *)cmd_buf);	// get command length

    uint8_t *cmd_buf_pntr = &(*cmd_buf);	// point to start of received string
    cmd_buf_pntr++;		// skip command identifier
    // check if all chars are valid hex chars
    while (*cmd_buf_pntr) {
        if (!isxdigit (*cmd_buf_pntr))
            return ERROR;
        ++cmd_buf_pntr;
    }
    cmd_buf_pntr = &(*cmd_buf);	// reset pointer
    

   // speed validation/negotiation handshake for CanHacker
   if (cmd_len==7)
   {
      if (strncmp((char *)cmd_buf,"CFFFFFF",7)==0)
      {
        usb_puts((uint8_t*)"FFFFFF");
        return CR;
      }
   }
    

    uint8_t tmp_regdata;	// temporary used for register data

    switch (*cmd_buf_pntr) {
            // get serial number
        case GET_SERIAL:
            usb_putc (GET_SERIAL);
            usb_puts ((uint8_t *)USBCAN_SERIAL);
            return CR;
      
            // get hard- and software version
        case GET_VERSION:
            usb_putc (GET_VERSION);
            usb_byte2ascii (HW_VER);
            usb_byte2ascii (SW_VER);
            return CR;

            // get only software version
        case GET_SW_VERSION:
            usb_putc (GET_SW_VERSION);
            usb_byte2ascii (SW_VER_MAJOR);
            usb_byte2ascii (SW_VER_MINOR);
            return CR;
      
            // toggle time stamp option
        case TIME_STAMP:
            if (cmd_len>1)
            {
              cmd_buf_pntr++;
              if (*cmd_buf_pntr == '0')
                ram_timestamp_status = 0;
              else if (*cmd_buf_pntr == '1')
                ram_timestamp_status = 0xA5;  // enable time stamp
              else 
                return ERROR;
            } 
            else
            {  // toggle on/off
              // read stored status
              ram_timestamp_status = eeprom_read_byte (&ee_timestamp_status);
              // toggle status
              if (ram_timestamp_status != 0)
                  ram_timestamp_status = 0;	// disable time stamp
              else {
                  ram_timestamp_status = 0xA5;	// enable time stamp
                }
            }

            timestamp = 0;	// reset time stamp counter
            // store new status
            eeprom_write_byte (&ee_timestamp_status, ram_timestamp_status);
            return CR;
      
            // read status flag
        case READ_STATUS:
            // check if CAN controller is in reset mode
            if (!is_in_normal_mode()) // if (!CHECKBIT (CAN_flags, BUS_ON))
                return ERROR;
      
            usb_putc (READ_STATUS);
            usb_byte2ascii (read_status()); // ((uint8_t) (CAN_flags >> 8));
      
            // turn off Bus Error indication
            clear_bus_errors();  
            return CR;
      
            // set AMR
        case SET_AMR:
           {
            // check valid cmd length and if CAN was initialized before
            if (cmd_len != 9)
                return ERROR;
           unsigned long reg;
           if (convert_string_to_int( (char*)(++cmd_buf_pntr), &reg, 8 ) == -1)
             return ERROR;
           set_fixed_filter_mask(reg);
           return CR;
           }
            // set ACR
        case SET_ACR:
            {
            // check valid cmd length and if CAN was initialized before
            if (cmd_len != 9)
                return ERROR;
      
           unsigned long reg;
           if (convert_string_to_int( (char*)(++cmd_buf_pntr), &reg, 8 ) == -1)
             return ERROR;            
           set_fixed_filter_pattern(reg);
           return CR;
            }
            // set bitrate via BTR
        case SET_BTR:
            {
            if (cmd_len != 5)
                return ERROR;	// check valid cmd length
            // check if CAN controller is in reset mode
            if (is_in_normal_mode())
                return ERROR;

           unsigned long BTR0;
           unsigned long BTR1;
           if (convert_string_to_int( (char*)(++cmd_buf_pntr), &BTR0, 2 ) == -1)
             return ERROR;
           cmd_buf_pntr += 2;
           if (convert_string_to_int( (char*)(cmd_buf_pntr), &BTR1, 2 ) == -1)
             return ERROR;

           unsigned long BRP = BTR0 & 0x3F;
           unsigned long TSEG1 = BTR1 & 0xF;
           unsigned long TSEG2 = (BTR1>>4) & 0x7;
           // calculate the baudrate from equation: rate = f_XTAL / ( 2*(1+BRP)*(3 + TSEG1 + TSEG 2) ), where f_XTAL = 16Mhz for CANUSB

           unsigned long bitrate = 16000000 / (2*(1+BRP)*(3+TSEG1+TSEG2));
           
           if (init_module( bitrate ))
             return CR;
           return ERROR;
            }         
            // set fix bitrate
        case SET_BITRATE:
            {
            if (cmd_len != 2)
                return ERROR;	// check valid cmd length
      
            // check if CAN controller is in reset mode
            if (is_in_normal_mode())
                return ERROR;

           uint8_t btr = *(++cmd_buf_pntr);
           unsigned long bitrate = 0;
           switch (btr)
           {
             case '0': 
               bitrate = 10000;
               break;
             case '1': 
               bitrate = 20000;
               break;
             case '2': 
               bitrate = 50000;
               break;
             case '3': 
               bitrate = 100000;
               break;
             case '4': 
               bitrate = 125000;
               break;
             case '5': 
               bitrate = 250000;
               break;
             case '6': 
               bitrate = 500000;
               break;
             case '7': 
               bitrate = 800000;
               break;
             case '8': 
               bitrate = 1000000;
               break;
             default:
               return ERROR;
           }
           
           if (init_module( bitrate ))
             return CR;
           return ERROR;
            }
            // open CAN channel
        case OPEN_CAN_CHAN:
            // return error if controller is not initialized or already open
          if (is_in_normal_mode())
            return ERROR;

            if (switch_mode(MODE_NORMAL))
              return CR;
            return ERROR;
      
            // close CAN channel
        case CLOSE_CAN_CHAN:
                
            // check if CAN controller is in reset mode
          if (!is_in_normal_mode())
            return ERROR;

            if (switch_mode(MODE_CONFIG))
              return CR;
            return ERROR;

            // send 11bit ID message
        case SEND_R11BIT_ID:
            // check if CAN controller is in reset mode or busy
            if (!is_in_normal_mode())
              return ERROR;
              
            // check valid cmd length (only 5 bytes for RTR)  
            if (cmd_len != 5)
                return ERROR;
      
            CAN_tx_msg.rtr = 1;	// remote transmission request    
      
            // store std. frame format
            CAN_tx_msg.format = 0;
            // store ID
            CAN_tx_msg.id = ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            // store data length
            CAN_tx_msg.len = ascii2byte (++cmd_buf_pntr);
      
            // if transmit buffer was empty send message
            return transmit_CAN ();
      
        case SEND_11BIT_ID:
            // check if CAN controller is in reset mode or busy
            if (!is_in_normal_mode())
              return ERROR;
      
            if ((cmd_len < 5) || (cmd_len > 21))
                return ERROR;	// check valid cmd length
      
            CAN_tx_msg.rtr = 0;	// no remote transmission request
      
            // store std. frame format
            CAN_tx_msg.format = 0;
            // store ID
            CAN_tx_msg.id = ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            // store data length
            CAN_tx_msg.len = ascii2byte (++cmd_buf_pntr);
            // check number of data bytes supplied against data lenght byte
            if (CAN_tx_msg.len != ((cmd_len - 5) / 2))
                return ERROR;
      
            // check for valid length
            if (CAN_tx_msg.len > 8)
                return ERROR;
            else {		// store data
                // cmd_len is no longer needed, so we can use it as counter here
                for (cmd_len = 0; cmd_len < CAN_tx_msg.len; cmd_len++) {
                    cmd_buf_pntr++;
                    CAN_tx_msg.data[cmd_len] = ascii2byte (cmd_buf_pntr);
                    CAN_tx_msg.data[cmd_len] <<= 4;
                    cmd_buf_pntr++;
                    CAN_tx_msg.data[cmd_len] += ascii2byte (cmd_buf_pntr);
                }
            }
            // if transmit buffer was empty send message
            return transmit_CAN ();
      
            // send 29bit ID message
        case SEND_R29BIT_ID:
            // check if CAN controller is in reset mode or busy
            if (!is_in_normal_mode())
              return ERROR;
      
            if (cmd_len != 10)
                return ERROR;	// check valid cmd length
      
            CAN_tx_msg.rtr = 1;	// remote transmission request
      
            // store ext. frame format
            CAN_tx_msg.format = 1;
            // store ID
            CAN_tx_msg.id = ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            // store data length
            CAN_tx_msg.len = ascii2byte (++cmd_buf_pntr);
            // if transmit buffer was empty send message
            return transmit_CAN ();
      
        case SEND_29BIT_ID:
            // check if CAN controller is in reset mode or busy
            if (!is_in_normal_mode())
              return ERROR;
      
            if ((cmd_len < 10) || (cmd_len > 26))
                return ERROR;	// check valid cmd length
      
            CAN_tx_msg.rtr = 0;	// no remote transmission request
      
            // store ext. frame format
            CAN_tx_msg.format = 1;
            // store ID
            CAN_tx_msg.id = ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            CAN_tx_msg.id <<= 4;
            CAN_tx_msg.id += ascii2byte (++cmd_buf_pntr);
            // store data length
            CAN_tx_msg.len = ascii2byte (++cmd_buf_pntr);
            // check number of data bytes supplied against data lenght byte
            if (CAN_tx_msg.len != ((cmd_len - 10) / 2))
                return ERROR;
      
            // check for valid length
            if (CAN_tx_msg.len > 8)
                return ERROR;
            else {		// store data
                // cmd_len is no longer needed, so we can use it as counter here
                for (cmd_len = 0; cmd_len < CAN_tx_msg.len; cmd_len++) {
                    cmd_buf_pntr++;
                    CAN_tx_msg.data[cmd_len] = ascii2byte (cmd_buf_pntr);
                    CAN_tx_msg.data[cmd_len] <<= 4;
                    cmd_buf_pntr++;
                    CAN_tx_msg.data[cmd_len] += ascii2byte (cmd_buf_pntr);
                }
            }
            // if transmit buffer was empty send message
            return transmit_CAN ();
      
            // read Error Capture Register
            // read Arbitration Lost Register
        case READ_ECR:
            if (!is_in_normal_mode())
              return ERROR;
            
            usb_putc (READ_ECR);
            usb_byte2ascii (0 );    // FIXME: MCP2515 doesn't support this! What to do?
            return CR;

        case READ_ALCR:
            // check if CAN controller is in reset mode
            if (!is_in_normal_mode())
              return ERROR;
      
            usb_putc (READ_ALCR);
            usb_byte2ascii (0 );    // FIXME: MCP2515 doesn't support this! What to do?
            return CR;
      
            // read SJA1000 register
        case READ_REG:
            if (cmd_len != 3)
                return ERROR;	// check valid cmd length
            // cmd_len is no longer needed, so we can use it as buffer
            // get register number
            cmd_len = ascii2byte (++cmd_buf_pntr);
            cmd_len <<= 4;
            cmd_len |= ascii2byte (++cmd_buf_pntr);
            usb_putc (READ_REG);
            usb_byte2ascii (read_CAN_reg (cmd_len));
            return CR;
      
            // write SJA1000 register
        case WRITE_REG:
            if (cmd_len != 5)
                return ERROR;	// check valid cmd length
      
            // cmd_len is no longer needed, so we can use it as buffer
            // get register number
            cmd_len = ascii2byte (++cmd_buf_pntr);
            cmd_len <<= 4;
            cmd_len |= ascii2byte (++cmd_buf_pntr);
            // get register data
            tmp_regdata = ascii2byte (++cmd_buf_pntr);
            tmp_regdata <<= 4;
            tmp_regdata |= ascii2byte (++cmd_buf_pntr);
            write_CAN_reg (cmd_len, tmp_regdata);
            return CR;
      
        case LISTEN_ONLY:
            // return error if controller is not initialized or already open
            if (get_operation_mode() != MODE_CONFIG)
              return ERROR;

            if (switch_mode(MODE_LISTEN))
              return CR;
            return ERROR;
      
            // end with error on unknown commands
        default:
            return ERROR;
    }				// end switch

    // we should never reach this return
    return ERROR;
}				// end exec_usb_cmd


/*
**---------------------------------------------------------------------------
**
** Abstract: Convert 1 char ASCII to 1 low nibble binary
**
**
** Parameters: Pointer to ASCII char
**
**
** Returns: Byte value
**
**
**---------------------------------------------------------------------------
*/
uint8_t
ascii2byte (uint8_t * val)
{
    uint8_t temp = *val;

    if (temp > 0x60)
        temp -= 0x27;		// convert chars a-f
    else if (temp > 0x40)
        temp -= 0x07;		// convert chars A-F
    temp -= 0x30;		// convert chars 0-9

    return temp & 0x0F;
}


namespace UsbCAN {

int init_protocol()
{
    Timer1.initialize(1000); // 1 millisec
    Timer1.attachInterrupt( IncreaseTimestamp );

    timestamp = 0;

    // read status of time stamp setting
    ram_timestamp_status = eeprom_read_byte (&ee_timestamp_status);

    sei ();			// enable global interrupts
    return 1;
}  
  
void dispatch_CAN_message( tCAN * message )
{

  int i;
          // check frame format
            if (!message->header.eid) {	// Standart Frame
                if (!message->header.rtr) {
                    usb_putc (SEND_11BIT_ID);
                }		// send command tag
                else {
                    usb_putc (SEND_R11BIT_ID);
                }
      
                // send high byte of ID
                if (((message->id >> 8) & 0x0F) < 10)
                    usb_putc (((uint8_t) (message->id >> 8) & 0x0F) + 48);
                else
                    usb_putc (((uint8_t) (message->id >> 8) & 0x0F) + 55);
                // send low byte of ID
                usb_byte2ascii ((uint8_t) message->id & 0xFF);
            }
            else {		// Extented Frame
                if (!message->header.rtr) {
                    usb_putc (SEND_29BIT_ID);
                }		// send command tag
                else {
                    usb_putc (SEND_R29BIT_ID);
                }
                // send ID bytes
                usb_byte2ascii ((uint8_t) (message->id >> 24) & 0xFF);
                usb_byte2ascii ((uint8_t) (message->id >> 16) & 0xFF);
                usb_byte2ascii ((uint8_t) (message->id >> 8) & 0xFF);
                usb_byte2ascii ((uint8_t) message->id & 0xFF);
            }
            
            // send data length code
            usb_putc (message->header.length + '0');
            if (!message->header.rtr) {	// send data only if no remote frame request
                // send data bytes
                for (i = 0; i <  message->header.length; i++)
                    usb_byte2ascii (message->data[i]);
            }
            // send time stamp if required
            if (ram_timestamp_status != 0) {
                usb_byte2ascii ((uint8_t) (timestamp >> 8));
                usb_byte2ascii ((uint8_t) timestamp);
            }
            // send end tag
            usb_putc (CR);
        
}

int  handle_host_message( char * cmd_buf ) 
{
    // Execute USB command and return status to terminal
    uint8_t ret = exec_usb_cmd ((uint8_t*)cmd_buf);
    usb_putc (ret);
    if (ret==CR)
       return 1;
     return 0; 
}	

} // namespace

