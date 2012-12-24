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
#ifndef _SARDINE_H
#define _SARDINE_H
#include <mcp2515.h> 

#define MODE_NORMAL 0
#define MODE_SLEEP 1
#define MODE_LOOPBACK 2
#define MODE_LISTEN 3
#define MODE_CONFIG 4

#define send_to_host(...) {\
  printf("{");  \
  printf(__VA_ARGS__);  \
  printf("}\r"); } 

#define _send_to_host(...) { printf(__VA_ARGS__); }

int convert_string_to_int( char * src, unsigned long * dest, int byteCount );
int convert_string_to_int( char * src, unsigned long * dest );
int convert_ascii_to_nibble(char c);


int send_CAN_msg(tCAN * msg);
void set_keepalive_timeout( unsigned long timeout );
int init_module( unsigned long baudrate );
int set_fixed_filter_pattern( unsigned long pattern );
int set_fixed_filter_mask( unsigned long mask );
int is_in_normal_mode();
unsigned int get_operation_mode();

int switch_mode( unsigned int mode);

uint8_t read_status();
void clear_bus_errors();

uint8_t read_CAN_reg (uint8_t reg);
void write_CAN_reg (uint8_t reg, uint8_t data);


#endif

