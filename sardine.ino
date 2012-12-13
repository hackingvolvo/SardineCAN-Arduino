/* Sardine CAN (Open Source J2534 device) - Arduino firmware - version 0.2 alpha
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
#include <Canbus.h>
#include <defaults.h>
#include <global.h>
#include <mcp2515.h>
#include <mcp2515_defs.h> 
#include <LiquidCrystal.h>
#include <stdio.h>
#include "sardine.h"

char foo;  // for the sake of Arduino header parsing anti-automagic. Remove and prepare yourself for headache.

//#define LCD 

// initialize the library with the numbers of the interface pins
#ifdef LCD
LiquidCrystal lcd(7, 8, 3, 4, 5, 6);
#endif

  tCAN manual_msg;  // manually constructed message manipulated by following commands: id,data,rtr,send
  
  tCAN keepalive_msg;  // hard coded keepalive message
  unsigned long last_keepalive_msg;
  unsigned long keepalive_timeout; // timeout in 1/10 seconds. 0=keepalive messaging disabled


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

#define send_to_host(...) {\
  printf("{");  \
  printf(__VA_ARGS__);  \
  printf("}\n"); } 

#define _send_to_host(...) { printf(__VA_ARGS__); }


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
int convert_ascii_char_to_nibble(char c)
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

int convert_hex_to_int( char * src, unsigned long * dest )
  {
  int nibble=0;
  int index=0;
  uint32_t number = 0;
  while ( src[index] && ((nibble=convert_ascii_char_to_nibble(src[index]))!=16) )
    {
    number *= 16;
    number += nibble;
    index++;  
    }
  *dest = number;
  if (nibble==16)
    return -1;
  return 0;
  }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int tokenize_cmd( char * cmd, my_param * arglist, int maxarguments)
  {
  char * saveptr;
  int argcount;
  char * data;
  unsigned long number; 
    
  arglist[0].str = strtok_r(cmd, " ", &saveptr);
  if (arglist[0].str==NULL)
    {
    send_to_host("!empty_command!");
    return -1;
    }
  arglist[0].type=STRING;

  argcount=1;
  arglist[argcount].datalen=0;
  arglist[argcount].type=NULL;
  arglist[argcount].str=NULL;
  boolean inChunk=false;
  while ( (argcount<maxarguments) && (arglist[argcount].datalen<MAX_CHUNK_SIZE) && (data=strtok_r(NULL," ",&saveptr) ) )
    {
    boolean processed=false;
    if (data[0]=='[')
      {
      // beginning of a chunk
      if (inChunk)
        {
        // already inside chunk!
        send_to_host("!invalid_arg #%d: %s - embedded data chunks not supported!",argcount+1,data);
        return -1;
        }
      inChunk=true;      
      arglist[argcount].datalen=0;
      arglist[argcount].type=DATA_CHUNK;
      if (strlen(data)>1)  // remove the [ if it is attached to the first byte or ], and the defer parsing the rest
        {
        data++;
        processed=false;
        } else
        processed=true;
      } 
  
    if (data[strlen(data)-1]==']')
      {
      // ending of a chunk
      if (!inChunk)
        {
        // not inside chunk!
        send_to_host("!invalid_arg #%d: %s - not inside data chunk!",argcount+1,data);
        return -1;
        }
      inChunk=false;

      if (strlen(data)>1)  // remove the ] attached to the last byte and then parse it
        {
        data[strlen(data)-1]=0;
        if ( (convert_hex_to_int(data,&number)==-1) || (number>255) )
          {
          send_to_host("!invalid_arg #%d: %s",argcount+1,data);
          return -1;
          } else
          {
          arglist[argcount].data[arglist[argcount].datalen]=number;
          }
        arglist[argcount].datalen++;
        }
      argcount++;      
      arglist[argcount].datalen=0;
      arglist[argcount].type=NULL;
      arglist[argcount].str=NULL;
      processed=true;
      } 

    if (inChunk)      
        {
        // in the middle of the chunk
        if ( (convert_hex_to_int(data,&number)==-1) || (number>255) )
          {
          send_to_host("!invalid_arg #%d/%x: %s",argcount+1,arglist[argcount].datalen+1,data);
          return -1;
          } 
        else
          {
          arglist[argcount].data[arglist[argcount].datalen]=number;
          }
        arglist[argcount].datalen++;
        processed=true;
        } 
   
    if (!processed)   
      {
        unsigned long number;
        // consider parameter as hexadecimal value only if is starts with "0x"
        if ( (strncmp(data,"0x",2)==0) && (convert_hex_to_int(&data[2],&number) != -1) )
        {
          // hexadecimal value
          arglist[argcount].type = VALUE;
          arglist[argcount].value = number;
        }
        else
        {
          // default to string
          arglist[argcount].type = STRING;
          arglist[argcount].str = data;
        }
        argcount++;
        arglist[argcount].datalen=0;
        arglist[argcount].type=NULL;
        arglist[argcount].str=NULL;        
      }
    }
    
  if (inChunk)
    {
     send_to_host("!invalid_arg - data chunk not closed!");
     return -1;
    }
    
  return argcount;
  }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int send_msg(tCAN * msg, bool silent)
  {   
   char buffer[256]; 
 //   printf("sending..\n");

// FIXME: here we need to see if mcp2512 send buffer is full etc!
  uint8_t status = mcp2515_read_status(SPI_READ_STATUS);
  if (status!=0)
   {
    if (!silent)
      send_to_host("!mcp2512_read_status: 0x%x",status);
   }
  
  /*
  uint8_t data;
  data = mcp2515_read_register(TXB0CTRL);
  Serial.print("TXB0CTRL: ");
  for (int i=0;i<8;i++)
    Serial.print( (data>>(7-i))&1 );
  Serial.println("");
  */
#ifdef LCD
  lcd.setCursor(0,0);
#endif
if (msg->header.rtr)
      {
#ifdef LCD
      lcd.print("rtr  ");
#endif
      } else
      {
#ifdef LCD
      lcd.print("sent ");        
#endif
      }
  sprintf(buffer,"%02x %02x %02x %02x", (uint8_t)(msg->id>>24),(uint8_t)((msg->id>>16)&0xff),(uint8_t)((msg->id>>8)&0xff),(uint8_t)(msg->id&0xff) );
#ifdef LCD
  lcd.setCursor(5,0);
  lcd.print(buffer);
  lcd.setCursor(0,1);
#endif
  int i;
  for (i=0;i<msg->header.length;i++)
    {
    sprintf(buffer,"%02x", msg->data[i]);
#ifdef LCD
    lcd.print(buffer);
#endif
  }
#ifdef LCD
  while (i<16)
    {
    lcd.print(" ");
    i++;
    }
#endif

  int ret=mcp2515_send_message_J1939(msg);  // ret=0 (buffers full), 1 or 2 = used send buffer

  if (ret)
    {
    if (!silent)
      send_to_host("!send_ok");  // FIXME: we return the result of adding the msg to MCP2515 internal buffer, not actually the result of sending it! 
    }
  else
    {
#ifdef LCD
    lcd.setCursor(0,0);
    lcd.print("ovfl");  // overflow
#endif
    send_to_host("!send_buffer_overflow");  // FIXME: we return the result of adding the msg to MCP2515 internal buffer, not actually the result of sending it! 
    }
  
  return ret;
  }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned long int data_chunk_to_header_id( my_param * arg )
  {
  if ( (arg->type==DATA_CHUNK) && (arg->datalen>=4) )
    {
    return (((uint32_t)arg->data[0])<<24)+(((uint32_t)arg->data[1])<<16)+(((uint32_t)arg->data[2])<<8)+(uint32_t)arg->data[3];
    }
    return -1;
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

int parse_cmd( char * msg )
  {
  char * cmd;
  my_param args[4];
  int argcount;
  tCAN temp_msg;
  
  argcount = tokenize_cmd(msg,args,4);
  cmd=args[0].str;
    
  if (strcmp(cmd,":heater_work_status")==0)
    {
    temp_msg.id = 0x000ffffe;
    temp_msg.header.rtr = 0;
    temp_msg.header.eid = 1;
    temp_msg.header.length = 8;
    temp_msg.data[0] = 0xcd;
    temp_msg.data[1] = 0x40;
    temp_msg.data[2] = 0xa6;
    temp_msg.data[3] = 0x5f;
    temp_msg.data[4] = 0x32;
    temp_msg.data[5] = 0x01;
    temp_msg.data[6] = 0x00;
    temp_msg.data[7] = 0x00;
    send_msg(&temp_msg,false);    
    } 
  else if (strcmp(cmd,":coolant_temp")==0)
    {
    temp_msg.id = 0x000ffffe;
    temp_msg.header.rtr = 0;
    temp_msg.header.eid = 1;
    temp_msg.header.length = 8;
    temp_msg.data[0] = 0xcd;
    temp_msg.data[1] = 0x40;
    temp_msg.data[2] = 0xa6;
    temp_msg.data[3] = 0x5f;
    temp_msg.data[4] = 0x30;
    temp_msg.data[5] = 0x01;
    temp_msg.data[6] = 0x00;
    temp_msg.data[7] = 0x00;                        
    send_msg(&temp_msg,false);    
    } 
  else if (strcmp(cmd,":start_heater")==0)
    {
    temp_msg.id = 0x000ffffe;
    temp_msg.header.rtr = 0;
    temp_msg.header.eid = 1;
    temp_msg.header.length = 8;
    temp_msg.data[0] = 0xcf;
    temp_msg.data[1] = 0x40;
    temp_msg.data[2] = 0xb1;
    temp_msg.data[3] = 0x5f;
    temp_msg.data[4] = 0x3b;
    temp_msg.data[5] = 0x01;
    temp_msg.data[6] = 0x01;
    temp_msg.data[7] = 0x84;                        
    send_msg(&temp_msg,false);    
    } 
  else if (strcmp(cmd,":stop_heater")==0)
    {
    temp_msg.id = 0x000ffffe;
    temp_msg.header.rtr = 0;
    temp_msg.header.eid = 1;
    temp_msg.header.length = 8;
    temp_msg.data[0] = 0xcf;
    temp_msg.data[1] = 0x40;
    temp_msg.data[2] = 0xb1;
    temp_msg.data[3] = 0x5f;
    temp_msg.data[4] = 0x3b;
    temp_msg.data[5] = 0x01;
    temp_msg.data[6] = 0x01;
    temp_msg.data[7] = 0x80;                        
    send_msg(&temp_msg,false);    
    } 
  else if (strcmp(cmd,":ping")==0)
    {
    send_to_host("!pong");
    }
  else if (strcmp(cmd,":version")==0)
    {
    send_to_host("!version 0.2");
    }
  else if (strcmp(cmd,":keepalive")==0)
    {
    if ( (argcount<2) || (args[1].type != VALUE) )
      {
      send_to_host("!keepalive: not enough parameters!\n");
      } else
     {
     keepalive_timeout = args[1].value;
    }  
    } 
  else if (strcmp(cmd,":send")==0)  // sends previously set data as normal CAN message
    {
    manual_msg.header.rtr = 0;
    if (send_msg(&manual_msg,false)==0)
      {
      return -1;
      }
    }
  else if (strcmp(cmd,":rtr")==0)  // sends previously set data as RTR message
    {
    manual_msg.header.rtr = 1;
    if (send_msg(&manual_msg,false)==0)
      {
      return -1;
      }
    } 
  else if (strcmp(cmd,":id")==0)  // set header for the upcoming message
    {
    if ( (argcount<2) || (args[1].type != DATA_CHUNK) || (args[1].datalen != 4) )
      {
      send_to_host("!id_invalid_number_of_args");
      return -1;
      }
    manual_msg.id = data_chunk_to_header_id(&args[1]);
    } 
  else if (strcmp(cmd,":data")==0)  // set data for the upcoming message
    {
    if ( (argcount<2) || (args[1].type != DATA_CHUNK) )
      {
      send_to_host("!data_invalid_number_of_args");
      return -1;
      }
    for (int i=0;i<args[1].datalen;i++)
      manual_msg.data[i] = args[1].data[i];
    manual_msg.header.length = args[1].datalen;
    }
  else if (strcmp(cmd,":msg")==0)
    {
    if ( (argcount<3) || (args[1].type != STRING) || (args[2].type != DATA_CHUNK) || (args[2].datalen<4) )
      {
      send_to_host("!msg_too_few_args (%d)",args[2].datalen);
      return -1;
      }
     // FIXME: ignoring flags for now, assuming this is normal CAN bus message.
    temp_msg.id = data_chunk_to_header_id(&args[2]);
    for (int i=4;i<args[2].datalen;i++)
      temp_msg.data[i-4] = args[2].data[i];
    temp_msg.header.length = args[2].datalen-4;
    temp_msg.header.rtr = 0;
    if (send_msg(&temp_msg,false)==0)
      return -1;
    }
  
  return 0;
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
 lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.print("Waiting for msg..");
  lcd.display();
  lcd.setCursor(1, 1);  
#endif

 if(Canbus.init(CANSPEED_125))  /* Initialise MCP2515 CAN controller at the specified speed */
  {
    send_to_host("!CAN_init_ok");
  } else
  {
    send_to_host("!CAN_init_failed");
  } 
  
  // reset device to loopback mode and one-shot mode
//  mcp2515_write_register(CANCTRL, (2<<5) + (1<<3) );   
  
  // reset device to normal mode
    mcp2515_bit_modify(CANCTRL, (1<<REQOP2)|(1<<REQOP1)|(1<<REQOP0), 0);
    
    // don't require interrupts from successful send
    mcp2515_bit_modify(CANINTE, (1<<TX0IF), 0);

//  delay(1000); 
  last_keepalive_msg=millis();
  
  // initialize the keep-alive message
  keepalive_msg.header.rtr = 0;
  keepalive_msg.header.eid = 1;
  keepalive_msg.header.length = 8;
  keepalive_msg.id = 0x000ffffe;
  keepalive_msg.data[0] = 0xd8;
  int i;
  for (i=1;i<8;i++)
    keepalive_msg.data[i] = 0x00;  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Only diagnostic response addresses will pass the filter 
bool does_pass_filter( tCAN * message )
{
  if ((message->id & 0xffff0000) == 0x00800000)
    return true;
  if (message->id == 0x0000072e)
    return true;  
  if (message->id == 0x00000001)
    return true;  
    
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void parse_heater_msgs( tCAN * message )
{
  // CEM ECU
  if (message->id==0x00800003)
  {
//    send_to_host("!msg from CEM");
    if ( 
    (message->data[0]==0xcd) &&
    (message->data[1]==0x40) &&
    (message->data[2]==0xe6) &&
    (message->data[3]==0x5f) )
    {
//      send_to_host("!msg from CPM");
      if (message->data[4]==0x32) 
      {
        // heater work status
        send_to_host("!heater_work_status %x",message->data[5]);
      } else
      if (message->data[4]==0x30) 
      {
        // coolant temp
        // 06 a0 == 1696 == 69.4
        // 06 ad == 1709 == 70.9
        // 06 c0 == 1728 == 72.4
        // 07 19 == 1817 == 81.7
        // slope = 12.3 / 121 = 0.10165 ~ 0.1
        // => 1696 - 682.7 = 1013.3 = 0 celcius
        // => (deg) = (x - 1013.3) * 0.1
        unsigned int temp = ( ((unsigned int)message->data[5])*256 + (unsigned int)message->data[6] - 1013 ) / 10 + 1;         
        send_to_host("!coolant_temp %d",temp);
      }
    }
    
  }  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void handle_CAN_messages()
{
  char msg[256];
  while (mcp2515_check_message())
    {
    tCAN message;
    char data[8];
    uint8_t status = mcp2515_get_message(  &message );
    if (status)  {
      parse_heater_msgs(&message);
      if (does_pass_filter(&message))
      {
#ifdef LCD
      lcd.setCursor(0,0);
#endif
if (message.header.rtr)
          {
          _send_to_host("{!msg cr");
#ifdef LCD
          lcd.print("rtr  ");
#endif
} else
          {
          _send_to_host("{!msg cn");
#ifdef LCD
          lcd.print("recv ");        
#endif
}
      if (message.header.eid)
        { 
          // extended frame format (29-bit CAN)
          _send_to_host("x");
        }
        else
        {
          // base frame format (11-bit CAN)
          _send_to_host("b");
        }
      // length of frame (actually id+payload)
      _send_to_host("%x [",message.header.length);
                  
      sprintf(msg,"%02x %02x %02x %02x", (uint8_t)(message.id>>24),(uint8_t)((message.id>>16)&0xff),(uint8_t)((message.id>>8)&0xff),(uint8_t)(message.id&0xff) );      
      _send_to_host("%s ",msg); //%2x %2x %2x %2x : ",status,(uint8_t)(message.id>>24),(uint8_t)((message.id>>16)&0xff),(uint8_t)((message.id>>8)&0xff),(uint8_t)(message.id&0xff));
#ifdef LCD
      lcd.setCursor(5,0);
      lcd.print(msg);
      lcd.setCursor(0,1);
#endif
      int i;
      for (i=0;i<message.header.length;i++)
        {
        sprintf(data,"%02x", message.data[i]);
#ifdef LCD
        lcd.print(data);
#endif
        if (i<message.header.length-1)
          _send_to_host("%s ",data)
        else
          _send_to_host("%s",data)
        }
        _send_to_host("]}\n");
#ifdef LCD
       // pad the lcd screen
      while (i<16)
        {
        lcd.print(" ");
        i++;
        }
#endif
          } // if (pass_filter..
          
       } // if (status)  {

    } // while (mcp2515_check_message())
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void handle_host_messages()
{
 int receivedByte;
 char msg[256];
 int index=0;
 while (Serial.available() > 0) {
	receivedByte = Serial.read();
          if  ( (receivedByte =='\n') || (receivedByte =='\r'))
          {
//          printf("parsing cmd..\n");
          index=0;
          if ( (msg[0]=='{') && (msg[strlen(msg)-1]=='}') )
            {
            // strip wavy brackets and parse the command
            msg[strlen(msg)-1]=0;
            if (parse_cmd(&msg[1])==-1)
              {
#ifdef LCD
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("erp");
              lcd.setCursor(4, 0);
              lcd.print(msg);
#endif
              }            
            }
            else
            {
#ifdef LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("inv");
          lcd.setCursor(4, 0);
          lcd.print(msg);
#endif
            send_to_host("!invalid_data_format '%s'",msg);
            }
          }
          else if (receivedByte =='\b')  // handle backspaces as well (since we might be testing functionality on terminal)
          {
            if (index>0)
              index--; 
          }
          else
          {
            msg[index++] = receivedByte;
            msg[index]=0;
	  }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {

  handle_host_messages();
  handle_CAN_messages();

  if (keepalive_timeout>0)
    if (millis()-last_keepalive_msg > keepalive_timeout*100)
      {
      send_msg(&keepalive_msg,false);
      last_keepalive_msg = millis();
      }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

