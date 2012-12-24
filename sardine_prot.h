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
#ifndef _SARDINE_PROT_H
#define _SARDINE_PROT_H

#include <mcp2515.h> 

namespace SardineProtocol
{
  
#define MAX_CHUNK_SIZE 16

enum  param_type { NOTHING, VALUE, STRING, DATA_CHUNK };

typedef struct {
  int type;
  char * str;
  unsigned long value;
  unsigned char data[MAX_CHUNK_SIZE];
  int datalen;
} my_param;

  
  
int init_protocol();
void dispatch_CAN_message( tCAN * message );
boolean handle_host_message( char * cmd_buf );

void module_init_failed();
void module_init_success();
  
}

#endif
