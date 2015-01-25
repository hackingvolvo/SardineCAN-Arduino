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
#ifndef _SARDINE_LED_H
#define _SARDINE_LED_H

#define ENABLE_LEDS
#define LED_PIN_STATUS A5
#define LED_PIN_CAN A4
#define LED_PIN_ERROR A3

typedef struct {
   bool enabled;
   bool pwr;  // is led now lit or off
   uint8_t mode; 
   unsigned long nextSwitch;
   unsigned int duration;  // duration of one/multiple blinks
   unsigned int multipleInterval;  // duration between multiple blinks
   unsigned int interval;  // duration between blink (or series of blinks)
   uint8_t blinkCount;
   uint8_t blinkIndex; 
   unsigned int pin;
} led;

#define LED_SINGLE_BLINK 0
#define LED_BLINK_LOOP 1

void SetLED( led * targetLED, unsigned int duration );
void SetBlinkLED( led * targetLED, unsigned int duration, unsigned int interval );
void SetMultipleBlinkLED( led * targetLED, unsigned int blinkCount, unsigned int duration, unsigned multipleInterval, unsigned int seriesInterval );
void ClearLED( led * targetLED );
void HandleLED( led * targetLED );

#endif

