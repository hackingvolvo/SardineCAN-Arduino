/* Sardine CAN (Open Source J2534 device) - Arduino firmware - version 0.3 alpha
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
#include <Arduino.h>
#include <mcp2515.h>
#include <defaults.h>
#include <global.h>
#include <mcp2515_defs.h> 
#include <stdio.h>
#include "sardine.h"
#include "sardine_prot.h"

namespace SardineProtocol 
{

  tCAN manual_msg;  // manually constructed message manipulated by following commands: id,data,rtr,send
  
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  

  
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
        if ( (convert_string_to_int(data,&number)==-1) || (number>255) )
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
        if ( (convert_string_to_int(data,&number)==-1) || (number>255) )
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
        if ( (strncmp(data,"0x",2)==0) && (convert_string_to_int(&data[2],&number) != -1) )
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

int send_msg(tCAN * msg, boolean silent)
  {   
  
  int ret=send_CAN_msg(msg);  
  if (ret)
  {
    if (!silent)
      send_to_host("!send_ok");  // 
  }
  else
    {
    send_to_host("!send_buffer_overflow");  // FIXME: we return the result of adding the msg to MCP2515 internal buffer, not actually the result of sending it! 
    }
  return ret;
  }

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
    set_keepalive_timeout( args[1].value );
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
    int i=0;
    for (;i<args[1].datalen;i++)
      manual_msg.data[i] = args[1].data[i];
    manual_msg.header.length = args[1].datalen;
    }
  else if (strcmp(cmd,":msg")==0)
    {
    if (argcount<3)
      {
      send_to_host("!msg_too_few_args (%d)",argcount);
      return -1;
      }
    if ( (args[1].type != STRING) || (args[2].type != DATA_CHUNK) )
      {
      send_to_host("!msg_invalid args");
      return -1;
      }
    if (args[2].datalen<4)
      {
      send_to_host("!msg_too_small_data_chunk (%d)",args[2].datalen);
      return -1;
      }
     // FIXME: ignoring flags for now, assuming this is normal CAN bus message.
    temp_msg.id = data_chunk_to_header_id(&args[2]);
    int i=4;
    for (;i<args[2].datalen;i++)
      temp_msg.data[i-4] = args[2].data[i];
    temp_msg.header.length = args[2].datalen-4;
    temp_msg.header.rtr = 0;
    if (send_msg(&temp_msg,false)==0)
      return -1;
    }
  
  return 0;
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

void dispatch_CAN_message( tCAN * message )
{
      char msg[256];
      parse_heater_msgs(message);
      
      if (message->header.rtr)
          {
          _send_to_host("{!msg cr");
      } else
          {
          _send_to_host("{!msg cn");
      }
      if (message->header.eid)
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
      _send_to_host("%x [",message->header.length);
                  
      sprintf(msg,"%02x %02x %02x %02x", (uint8_t)(message->id>>24),(uint8_t)((message->id>>16)&0xff),(uint8_t)((message->id>>8)&0xff),(uint8_t)(message->id&0xff) );      
      _send_to_host("%s ",msg); //%2x %2x %2x %2x : ",status,(uint8_t)(message.id>>24),(uint8_t)((message.id>>16)&0xff),(uint8_t)((message.id>>8)&0xff),(uint8_t)(message.id&0xff));
      int i;
      char data[8];
      for (i=0;i<message->header.length;i++)
        {
        sprintf(data,"%02x", message->data[i]);

        if (i<message->header.length-1)
          _send_to_host("%s ",data)
        else
          _send_to_host("%s",data)
        }
        _send_to_host("]}\n");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

boolean handle_host_message( char * cmd_buf )
{
  unsigned int msgLen = strlen(cmd_buf);
  
          if ( (cmd_buf[0]=='{') && (cmd_buf[msgLen-1]=='}') )
            {
            // strip wavy brackets and parse the command
            cmd_buf[msgLen-1]=0;
            int ret=parse_cmd(&cmd_buf[1]);
            if (ret==-1) 
              return 0;
            else return 1;
            }
            else
            {
            send_to_host("!invalid_data_format '%s'",cmd_buf);
            }
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int init_protocol()
{
  return 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void module_init_failed()
{
  send_to_host("!CAN_init_failed");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void module_init_success()
{
  send_to_host("!CAN_init_ok");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace

