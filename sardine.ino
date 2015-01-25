/* Sardine CAN (Open Source J2534 device) - Arduino firmware - version 0.4 alpha
**
** Copyright (C) 2012 Olaf @ Hacking Volvo blog (hackingvolvo.blogspot.com)
** Author: Olaf <hackingvolvo@gmail.com>
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

//#define DEBUG_MAIN
//#define DEBUG_FREE_MEM

#include <TimerOne.h>
#include <EEPROM.h>
#include <Canbus.h>
#include <defaults.h>
#include <global.h>
#include <mcp2515.h>
#include <mcp2515_defs.h> 
#include <LiquidCrystal.h>

#include "led.h"
#include "usbcan.h"
#include "sardine_prot.h"
#include <stdio.h>
#include "sardine.h"

char foo;  // for the sake of Arduino header parsing anti-automagic. Remove and prepare yourself for headache.

// ONE_SHOT_MODE tries to send message only once even if error occurs during transmit.  (see MCP2515 data sheet)
// If we are testing the Sardine CAN without any other CAN device on the network, then there will be no ACK signals acknowledging that
// transmit succeeded and thus sending fails. If this happens, MCP2515 will keep on sending the message forever and transmit buffers will eventually
// fill up. Also cheap ELM327 clones (with older firmware) do not support ACK-signaling, so a network consisting of MCP2515 + ELM327 does not work
// if ONE_SHOT_MODE is not enabled. You should however disable this when connecting Sardine CAN to a car
//#define ONE_SHOT_MODE

// In addition to fixed filter, we pass the replies to diagnostic messages (between Volvo CAN modules and VIDA) 
#define PASS_VOLVO_DIAGNOSTIC_MSGS

// Filter does not pass messages by default, since VIDA seems to crash at start if all CAN messages are transmitted to it. If you are not using VIDA but
// for example CAN Hacker, you can uncomment this define or use Lawicell 'M' and 'm' commands to set acceptance register (set mask to 0x0 to pass all messages).
//#define PASS_ALL_MSGS


//#define LCD

// initialize the library with the numbers of the interface pins
#ifdef LCD
//LiquidCrystal lcd(7, 8, 3, 4, 5, 6);
LiquidCrystal lcd(A0, A1, A2, A3, A4, A5);  // RS_pin, enable_pin, D4, D5, D6, D7
#endif

#ifdef ENABLE_LEDS
led status_LED;
led CAN_LED;
led error_LED;
#endif


#define USBCAN    // we are using CANUSB (Lawicel) / CAN232 format by default now

  
  tCAN keepalive_msg;  // hard coded keepalive message
  unsigned long last_keepalive_msg;
  unsigned long keepalive_timeout; // timeout in 1/10 seconds. 0=keepalive messaging disabled
  
 tCAN keepalive_msg2;  // hard coded keepalive message
  unsigned long last_keepalive_msg2;
  unsigned long keepalive_timeout2; // timeout in 1/10 seconds. 0=keepalive messaging disabled
    
  // filters
  unsigned long fixed_filter_pattern;
  unsigned long fixed_filter_mask;

  char msgFromHost[256]; // message that is being read from host
  int msgLen=0;
  
  uint8_t status;
  unsigned int errorFlags;


// =========  Here we enable us to use printf to write to host instead of having to use Serial.print!
// we need fundamental FILE definitions and printf declarations
static FILE uartout = {0} ;
// create a output function
// This works because Serial.write, although of
// type virtual, already exists.
static int uart_putchar (char c, FILE *stream)
{
  // convert newline to carriage return
  if (c == '\n') {
        uart_putchar('\r', stream);
    }
    Serial.write(c) ;
    return 0 ;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SetErrorStatus( unsigned int errStatus )
{
#ifdef DEBUG_MAIN
	printf("SetErrStatus: %d\n",errStatus);
#endif
	errorFlags |= errStatus;
	switch(errStatus)
	{
	case ERRSTATUS_OUT_OF_MEMORY:
		{
		SetBlinkLED( &error_LED, 50, 50 );
		}
		break;
	case ERRSTATUS_CAN_INIT_ERROR:
		{
	        ClearLED( &status_LED );
		SetBlinkLED( &error_LED, 200, 200 );
		}
		break;
	case ERRSTATUS_CAN_TX_BUFFER_OVERFLOW:
		{
		SetMultipleBlinkLED( &error_LED, 2, 100, 100, 800 );
		}
		break;
	case ERRSTATUS_CAN_RX_BUFFER_OVERFLOW:
		{
		SetMultipleBlinkLED( &error_LED, 3, 100, 100, 800 );
		}
		break;
	case ERRSTATUS_NONE:
		{
		ClearLED( &error_LED );
		}
		break;
		
	default:
		break;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ClearErrorStatus( unsigned int errStatus )
{
	errorFlags &= ~errStatus;
	SetErrorStatus(errorFlags); // update the leds
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SetStatus( uint8_t currStatus )
{
#ifdef DEBUG_MAIN
	printf("SetStatus: %d\n",currStatus);
#endif
	switch (currStatus)
	{
	case STATUS_INIT:
		{
		status = currStatus;
	    SetBlinkLED( &status_LED, 200, 800 );
		}
		break;
	case STATUS_READY:
		{
		status = currStatus;
	    SetLED( &status_LED, 0 );
		}
		break;
	case STATUS_UNRECOVERABLE_ERROR:
		{
		status = currStatus;
		ClearLED(&status_LED);
		}
		break;
	default:
		break;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int GetStatus()
{
	return status;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int convert_ascii_to_nibble(char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';
	if ((c >= 'A') && (c <= 'F'))
		return 10 + c - 'A';
	if ((c >= 'a') && (c <= 'f'))
		return 10 + c - 'a';
	return 16; // in case of error
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int convert_string_to_int( char * src, unsigned long * dest, int byteCount )
  {
  int nibble=0;
  int index=0;
  uint32_t number = 0;
  while ( (index<byteCount) && src[index] && ((nibble=convert_ascii_to_nibble(src[index]))!=16) )
    {
    number *= 16;
    number += nibble;
    index++;  
    }
  *dest = number;
  if (nibble==16)  // error converting ascii to byte
    return -1;
  return 0;
  }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int convert_string_to_int( char * src, unsigned long * dest )
{
  return convert_string_to_int( src, dest, 256 );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef LCD
void show_CAN_msg_on_LCD( tCAN * message, bool recv )
{
      char msg[32];
      char data[8];
      lcd.clear();
      lcd.setCursor(0,0);
      if (recv)
        lcd.print("rx ");
      else
        lcd.print("tx ");
      
      if (message->header.rtr)
          {
          lcd.print("r");
      } else
          {
          lcd.print(" ");        
      }
                  
      sprintf(msg,"%02x %02x %02x %02x", (uint8_t)(message->id>>24),(uint8_t)((message->id>>16)&0xff),(uint8_t)((message->id>>8)&0xff),(uint8_t)(message->id&0xff) );      
      lcd.setCursor(5,0);
      lcd.print(msg);
      lcd.setCursor(0,1);

      int i;
      for (i=0;i<message->header.length;i++)
        {
        sprintf(data,"%02x", message->data[i]);
        lcd.print(data);
        }
}
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t read_CAN_reg (uint8_t reg)
{
    // TODO: map SJA1000 registers to MCP2515 and vice-versa
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void write_CAN_reg (uint8_t reg,
	       uint8_t data)
{
    // TODO: map SJA1000 registers to MCP2515 and vice-versa
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int send_CAN_msg(tCAN * msg)
  {   
//Serial.println("sendcanmsg");
// FIXME: here we need to see if mcp2512 send buffer is full etc!


/*
  uint8_t data;
  data = mcp2515_read_register(TXB0CTRL);
  Serial.print("TXB0CTRL: ");
  for (int i=0;i<8;i++)
    Serial.print( (data>>(7-i))&1 );
  Serial.println("");
  */
#ifdef LCD
  show_CAN_msg_on_LCD(msg, false);
#endif

  SetLED(&CAN_LED, 50);
  int ret=mcp2515_send_message_J1939(msg);  // ret=0 (buffers full), 1 or 2 = used send buffer
  
  if (ret==0)
  {
#ifdef LCD
    lcd.setCursor(0,0);
    lcd.print("ovfl");  // overflow
#endif
  SetErrorStatus(ERRSTATUS_CAN_TX_BUFFER_OVERFLOW);

#ifndef USBCAN
    uint8_t status = mcp2515_read_status(SPI_READ_STATUS);
    if (status!=0)
    {
      send_to_host("!mcp2512_read_status: 0x%x",status);
    }
    send_to_host("!mcp2515_send_message_J1939: 0x%x",ret);
#endif
  }

  return ret; // FIXME: we return the result of adding the msg to MCP2515 internal buffer, not actually the result of sending it! 
  }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void set_keepalive_timeout( unsigned long timeout )
{
 keepalive_timeout = timeout;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
  NOTE: Now we use software filtering (below in does_pass_filter()) instead of hardware filtering capabilities of MCP2515)
  
void delete_filters()
{
	// turn off filters => receive any message
	mcp2515_write_register(RXB0CTRL, (1<<RXM1)|(1<<RXM0));
	mcp2515_write_register(RXB1CTRL, (1<<RXM1)|(1<<RXM0));
}

void create_standard_ecu_filter()
{
	// receive only filtered messages
	mcp2515_write_register(RXB0CTRL, (0<<RXM1)|(0<<RXM0));
	mcp2515_write_register(RXB1CTRL, (0<<RXM1)|(0<<RXM0));
    // TODO
}
*/


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

void checkRam()
 {
  unsigned int freeram = freeRam();
#ifdef DEBUG_FREE_MEM
if (freeram < 256)
  {
    Serial.print("Warning! Lo mem: ");
    Serial.println(freeRam());
  }
else
  {
    Serial.print("Free mem: ");
    Serial.println(freeRam());
  }
#endif


  if (freeram < 128)
  {
  SetErrorStatus(ERRSTATUS_OUT_OF_MEMORY);
  SetStatus(STATUS_UNRECOVERABLE_ERROR);
  }


}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// returns 1 if success, 0 if failed
int init_module( unsigned long baudrate )
{
#ifdef LCD
  lcd.setCursor(0,1);
  lcd.print("init ");
  char txt[16];
  sprintf(txt,"%lu  ",baudrate);
  lcd.print(txt);
#endif

  unsigned char mcp2515_speed;
 switch (baudrate)
 {
   case 125000:
     mcp2515_speed=CANSPEED_125;
     break;
   case 250000:
     mcp2515_speed=CANSPEED_250;
     break;
   case 500000:
     mcp2515_speed=CANSPEED_500;
     break;
   default:
#ifdef LCD
  lcd.print("unsup");
#endif
     // unsupported speed!
      return 0;      
 }
 
 if(!Canbus.init(mcp2515_speed))  /* Initialise MCP2515 CAN controller at the specified speed */
  {
    // initialization failed!
#ifdef LCD
  lcd.print("fail!");
#endif
#ifdef DEBUG_MAIN
	printf("mcp2515 init failed!");
#endif
    SetErrorStatus(ERRSTATUS_CAN_INIT_ERROR);
    return 0;
  }
  
  // we are in config mode by default, before opening the channel
  switch_mode(MODE_CONFIG);
    
  // don't require interrupts from successful send
  mcp2515_bit_modify(CANINTE, (1<<TX0IE), 0);

  // enable one-shot mode
#ifdef ONE_SHOT_MODE
  mcp2515_bit_modify(CANCTRL, (1<<OSM), (1<<OSM));
#else
  mcp2515_bit_modify(CANCTRL, (1<<OSM), 0);
#endif

  
  // roll-over: receiving message will be moved to receive buffer 1 if buffer 0 is full
  mcp2515_bit_modify(RXB0CTRL, (1<<BUKT), (1<<BUKT));
  
#ifdef LCD
  lcd.setCursor(11,1);
  lcd.print(" ok");
#endif

  return 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// maps MCP2515 status registers to SJA1000 format for Lawicell compatibility
uint8_t read_status()
{
  uint8_t mcp2515_eflg = mcp2515_read_register(EFLG);  

  uint8_t mcp2515_st = mcp2515_read_status(SPI_READ_STATUS);

  // SPI_READ_STATUS:
  // bit 0: CANINTF.RX0IF
  // bit 1: CANINTFL.RX1IF
  // bit 2: TXB0CNTRL.TXREQ
  // bit 3: CANINTF.TX0IF
  // bit 4: TXB1CNTRL.TXRREQ
  // bit 5: CANINTF.TX1IF
  // bit 6: TXB2CNTRL.TXREQ
  // bit 7: CANINTF.TX2IF
  
  uint8_t st = 0;  // Lawicel/USBCAN status

  // Bit 0 CAN receive FIFO queue full
  if (GETBIT(mcp2515_st,0) && GETBIT(mcp2515_st,1))  // both receive buffers full
    SETBIT(st,0);

  // Bit 1 CAN transmit FIFO queue full
  if (GETBIT(mcp2515_st,2) && GETBIT(mcp2515_st,4) && GETBIT(mcp2515_st,6) )  // all three receive buffers full
    SETBIT(st,1);

  // Bit 2 Error warning (EI), see SJA1000 datasheet
  if (GETBIT(mcp2515_eflg,0))  // EWARN
    SETBIT(st,2);

  // Bit 3 Data Overrun (DOI), see SJA1000 datasheet
  if (GETBIT(mcp2515_eflg,7) || GETBIT(mcp2515_eflg,6))  // overflow in one of the two receive buffers
    SETBIT(st,3);  

  // Bit 4 Not used.
  
  // Bit 5 Error Passive (EPI), see SJA1000 datasheet
   if (GETBIT(mcp2515_eflg,3) || GETBIT(mcp2515_eflg,4))  // either transmit of receive passive flag is set
    SETBIT(st,5); 
  
  // Bit 6 Arbitration Lost (ALI), see SJA1000 datasheet *
  uint8_t mcp2515_txb0ctrl = mcp2515_read_status(TXB0CTRL);
  uint8_t mcp2515_txb1ctrl = mcp2515_read_status(TXB1CTRL);
  uint8_t mcp2515_txb2ctrl = mcp2515_read_status(TXB2CTRL);
  if ( GETBIT(mcp2515_txb0ctrl,MLOA) || GETBIT(mcp2515_txb1ctrl,MLOA) || GETBIT(mcp2515_txb2ctrl,MLOA) )
    SETBIT(st,6);   
  
  // Bit 7 Bus Error (BEI), see SJA1000 datasheet **
  if (GETBIT(mcp2515_eflg,5))   // FIXME: does this bit mean Bus-off error (transmit errors>255) or one-time bus error??
    SETBIT(st,7);

  return st;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void clear_bus_errors()
{  
  // clear transmit buffers
  mcp2515_bit_modify(TXB0CTRL, (1<<TXREQ), 0);
  mcp2515_bit_modify(TXB1CTRL, (1<<TXREQ), 0);
  mcp2515_bit_modify(TXB2CTRL, (1<<TXREQ), 0);

  // clear interrupts
  mcp2515_write_register(CANINTF,0);
  
  // enter the configuration mode to clear all error counters
  mcp2515_bit_modify(CANCTRL, (1<<REQOP2)|(1<<REQOP1)|(1<<REQOP0), 1<<REQOP2);
  delay(1);
  // reset device to normal mode
  mcp2515_bit_modify(CANCTRL, (1<<REQOP2)|(1<<REQOP1)|(1<<REQOP0), 0);  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() { 
 
  Serial.begin(115200);
  
  //  Here we enable us to use printf to write to host instead of having to use Serial.print!
  // fill in the UART file descriptor with pointer to writer.
  fdev_setup_stream (&uartout, uart_putchar, NULL, _FDEV_SETUP_WRITE);
  // The uart is the standard output device STDOUT.
  stdout = &uartout ;

#ifdef LCD
  pinMode(A0,OUTPUT);
  pinMode(A1,OUTPUT);
  pinMode(A2,OUTPUT);
  pinMode(A3,OUTPUT);
  pinMode(A4,OUTPUT);
  pinMode(A5,OUTPUT);
  lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.print("Sardine CAN");
  lcd.setCursor(0, 1);  
  char version[16];
  sprintf(version,"v%d.%d",SW_VER_MAJOR,SW_VER_MINOR);
  lcd.print(version);
  lcd.display();
#endif

#ifdef ENABLE_LEDS
  status_LED.enabled=false;
  status_LED.pin= LED_PIN_STATUS;
  CAN_LED.enabled=false;
  CAN_LED.pin= LED_PIN_CAN;
  error_LED.enabled=false;
  error_LED.pin= LED_PIN_ERROR;
#endif  
  SetErrorStatus(ERRSTATUS_NONE);
  SetStatus(STATUS_INIT);

  // we have to initialize the CAN module anyway, otherwise SPI commands (read registers/status/get_operation_mode etc) hang during invocation
  if (init_module(125000)) // default speed
  {
    SetStatus(STATUS_CONFIG);
  } else
  {
    SetErrorStatus(ERRSTATUS_CAN_INIT_ERROR);
    SetStatus(STATUS_UNRECOVERABLE_ERROR);
  }
  // set MCP2551 to normal mode
  pinMode(MCP2551_STANDBY_PIN,OUTPUT);
  digitalWrite(MCP2551_STANDBY_PIN,LOW);
  
#ifdef USBCAN
  UsbCAN::init_protocol();
#else
  SardineProtocol::init_protocol();
#endif

//  delay(1000); 
  last_keepalive_msg=millis();
  
  // initialize the keep-alive message
  keepalive_msg.header.rtr = 0;
  keepalive_msg.header.eid = 1;
  keepalive_msg.header.length = 8;

/*
  keepalive_msg.id = 0x000ffffe;
  keepalive_msg.data[0] = 0xd8;
  int i;
  for (i=1;i<8;i++)
    keepalive_msg.data[i] = 0x00;  
*/
  keepalive_msg.id = 0x000ffffe;
  keepalive_msg.data[0] = 0xd8;
  int i;
  for (i=1;i<8;i++)
    keepalive_msg.data[i] = 0x00;  



  // initialize the keep-alive message
  last_keepalive_msg2=millis();

  keepalive_msg2.header.rtr = 0;
  keepalive_msg2.header.eid = 1;
  keepalive_msg2.header.length = 8;

  keepalive_msg2.id = 0x00400066;

  for (i=0;i<8;i++)
    keepalive_msg2.data[i] = 0x00;  
  
  keepalive_msg2.data[4] = 0x1f;
  keepalive_msg2.data[5] = 0x40;

/*
  // initialize the fixed filter to pass all
  fixed_filter_pattern = fixed_filter_mask = 0;
*/
  //init fixed filter to pass no messages initially (unless explicitly allowed by PASS_ALL_MSGS or PASS_VOLVO_DIAGNOSTIC_MSGS
  fixed_filter_pattern = 0;
  fixed_filter_mask = 0xFFFFFFFF;


  switch_mode(MODE_NORMAL);

#ifdef DEBUG_FREE_MEM
  unsigned int freemem = freeRam();
  Serial.print("free mem: ");
  Serial.println(freemem);
#endif

 // keep-alive messages not enabled by default
  keepalive_timeout = 0;
//  keepalive_timeout2 = 50;
  keepalive_timeout2 = 0;


}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Only diagnostic response addresses will pass the filter 
bool does_pass_filter( tCAN * message )
{
#ifdef PASS_ALL_MSGS
  return true;
#endif
#ifdef PASS_VOLVO_DIAGNOSTIC_MSGS
  if ((message->id & 0xffff0000) == 0x00800000)
    return true;
  if (message->id == 0x0000072e)
    return true;  
  if (message->id == 0x00000001)
    return true;  
#endif

  // does pass fixed filter ?
  if ((message->id & fixed_filter_mask) == (fixed_filter_pattern & fixed_filter_mask) )
    return true;
    
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int set_fixed_filter_pattern( unsigned long pattern )
{
#ifdef LCD
  lcd.setCursor(0,0);
  lcd.print("ptrn ");
  char id[16];
  sprintf(id,"%02x %02x %02x %02x", (uint8_t)(pattern>>24),(uint8_t)((pattern>>16)&0xff),(uint8_t)((pattern>>8)&0xff),(uint8_t)(pattern&0xff) );     
  lcd.print(id);
#endif
  fixed_filter_pattern=pattern;
  return 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int set_fixed_filter_mask( unsigned long mask )
{
#ifdef LCD
  lcd.setCursor(0,1);
  lcd.print("mask ");
  char id[16];
  sprintf(id,"%02x %02x %02x %02x", (uint8_t)(mask>>24),(uint8_t)((mask>>16)&0xff),(uint8_t)((mask>>8)&0xff),(uint8_t)(mask&0xff) );     
  lcd.print(id);
#endif
  fixed_filter_mask=mask;
  return 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int switch_mode( unsigned int mode)
{
  mcp2515_bit_modify(CANCTRL, (1<<REQOP2)|(1<<REQOP1)|(1<<REQOP0), mode<<REQOP0);
  delay(1);
  unsigned int ret_mode = get_operation_mode();  
  if (ret_mode != mode)
  {
#ifdef DEBUG_MAIN
	printf("Switch_mode failed %d->%d!\n",ret_mode,mode);
#endif
    SetErrorStatus(ERRSTATUS_CAN_INIT_ERROR);
  }
  
  #ifdef LCD
  lcd.setCursor(0,0);
  lcd.print("mode: ");
  if (ret_mode != mode)
  {
      lcd.print("error!");
      return 0;
  } else
  switch (ret_mode)
  {
    case MODE_NORMAL:
      lcd.print("normal");
      break;
    case MODE_SLEEP:
      lcd.print("sleep");
      break;
    case MODE_CONFIG:
      lcd.print("config");
      break;
    case MODE_LISTEN:
      lcd.print("listen");
      break;
    case MODE_LOOPBACK:
      lcd.print("loopback");
      break;
    default:
      lcd.print("error!");
      return 0;
      break;
  }
#endif

  if ( (ret_mode==MODE_NORMAL) || (mode==MODE_LISTEN) )
    SetStatus(STATUS_READY);
  else if (ret_mode==MODE_CONFIG)
    SetStatus(STATUS_CONFIG);
  else 
    {
    SetStatus(STATUS_UNRECOVERABLE_ERROR); // we don't handle other modes currently  
    SetErrorStatus(ERRSTATUS_CAN_INIT_ERROR);
    }


  return (ret_mode == mode);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


unsigned int get_operation_mode()
{
  uint8_t canstat = mcp2515_read_register(CANSTAT);
  unsigned int mode = (canstat>>OPMOD0) & 0x7;
#ifdef LCD
  lcd.setCursor(14,0);
  switch(mode)
  {
    case MODE_NORMAL:
      lcd.print("NO");
      break;
    case MODE_SLEEP:
      lcd.print("SL");
      break;
    case MODE_CONFIG:
      lcd.print("CF");
      break;
    case MODE_LISTEN:
      lcd.print("LI");
      break;
    case MODE_LOOPBACK:
      lcd.print("LP");
      break;
    default:
      lcd.print("ER");
      break;
  }
#endif
  return mode;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int is_in_normal_mode()
{
  return (get_operation_mode() == MODE_NORMAL);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void handle_CAN_rx()
{
  while (mcp2515_check_message())
    {
   SetLED(&CAN_LED, 50);
      
    tCAN message;
//    char data[8];
    uint8_t status = mcp2515_get_message(  &message );
    if (status)  
    {
      if (does_pass_filter(&message))
      {
#ifdef LCD
        show_CAN_msg_on_LCD(&message,true);
#endif
    
#ifdef USBCAN
  return UsbCAN::dispatch_CAN_message(&message);
#else
  return SardineProtocol::dispatch_CAN_message(&message);
#endif
        
          } // if (pass_filter..
        
        // check if RX buffer overflow has occured since last receive
        uint8_t eflg = mcp2515_read_register(EFLG);
        if (EFLG & (1<<RX1OVR) )
        {
          SetErrorStatus(ERRSTATUS_CAN_RX_BUFFER_OVERFLOW);
        }
          
       } // if (status)  {

    } // while (mcp2515_check_message())
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int handle_cmd(char * cmd)
{
#ifdef USBCAN
  return UsbCAN::handle_host_message(cmd);
#else
  return SardineProtocol::handle_host_message(cmd);
#endif
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void handle_host_messages()
{
 int receivedByte;
 while (Serial.available() > 0) 
	{
	receivedByte = Serial.read();
    if  (receivedByte =='\r')
        {
#ifdef LCD
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print(msgFromHost);
#endif
           
        if (handle_cmd(msgFromHost)==0)
             {
#ifdef LCD
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("err");
              lcd.setCursor(4, 0);
              lcd.print(msgFromHost);
#endif
              }            
            msgLen=0;
            msgFromHost[msgLen]=0;
          }
    else if (receivedByte =='\b')  // handle backspaces as well (since we might be testing functionality on terminal)
        {
            if (msgLen>0)
            {
              msgFromHost[--msgLen]=0;
            }
        }
    else
        {
        if (receivedByte !='\n') // ignore linefeeds
            {
              msgFromHost[msgLen++] = receivedByte;
              msgFromHost[msgLen]=0;
			}
        }
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  handle_host_messages();
  handle_CAN_rx();



  if (keepalive_timeout>0)
    if (millis()-last_keepalive_msg > keepalive_timeout *100)
      {
      send_CAN_msg(&keepalive_msg);
      last_keepalive_msg = millis();
      }

  if (keepalive_timeout2>0)
    if (millis()-last_keepalive_msg2 > keepalive_timeout2 *100)
      {
      send_CAN_msg(&keepalive_msg2);
      last_keepalive_msg2 = millis();
      }
      
  checkRam();
  
#ifdef ENABLE_LEDS
			HandleLED( &status_LED );
			HandleLED( &CAN_LED );
			HandleLED( &error_LED );
#endif

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

