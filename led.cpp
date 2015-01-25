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
#include <Arduino.h>
#include "led.h"

void SetLED( led * targetLED, unsigned int duration )
{
#ifdef ENABLE_LEDS
//	printf("SetLED (%d): dur %d\n",targetLED->pin, duration);
	targetLED->mode = LED_SINGLE_BLINK;
  targetLED->nextSwitch = millis() + duration;
  targetLED->duration = duration;
  targetLED->enabled = targetLED->pwr = true;
//  digitalWrite( targetLED->pin, HIGH );
  analogWrite( targetLED->pin, 128);  // PWM
#endif
}

void SetBlinkLED( led * targetLED, unsigned int duration, unsigned int interval )
{
#ifdef ENABLE_LEDS
//	printf("SetBlinkLED (%d): dur %d, interval %d\n",targetLED->pin, duration, interval);
  targetLED->mode = LED_BLINK_LOOP;  
//  if (duration>0)
    targetLED->nextSwitch = millis() + duration;
//  else
 //   targetLED->nextSwitch = 0;
  targetLED->duration = duration;
  targetLED->interval = targetLED->multipleInterval = interval;
  targetLED->enabled = targetLED->pwr = true;
  targetLED->blinkCount = 1;
  targetLED->blinkIndex = 0;
//  digitalWrite( targetLED->pin, HIGH );
  analogWrite( targetLED->pin, 128);  // PWM
#endif
}

void SetMultipleBlinkLED( led * targetLED, unsigned int blinkCount, unsigned int duration, unsigned multipleInterval, unsigned int seriesInterval )
{
#ifdef ENABLE_LEDS
//	printf("SetMultipleBlinkLED (%d): blinkcount %d, dur %d, mul_interval %d, series_interval %d\n",targetLED->pin, blinkCount, duration, multipleInterval, seriesInterval );
  targetLED->mode = LED_BLINK_LOOP;  
  targetLED->nextSwitch = millis() + duration;
  targetLED->duration = duration;
  targetLED->interval = seriesInterval;
  targetLED->multipleInterval = multipleInterval;
  targetLED->enabled = targetLED->pwr = true;
  targetLED->blinkCount = blinkCount;
  targetLED->blinkIndex = 0;
//  digitalWrite( targetLED->pin, HIGH );
  analogWrite( targetLED->pin, 128);  // PWM
#endif
}

void ClearLED( led * targetLED )
{
#ifdef ENABLE_LEDS
  targetLED->enabled = false;
  targetLED->pwr = false;
#endif
}


void HandleLED( led * targetLED )
{
#ifdef ENABLE_LEDS
	unsigned long currTime = millis();
	if (targetLED->enabled)
	{
		switch (targetLED->mode)
		{
		case LED_SINGLE_BLINK:
			{
				// duration == 0 -> led stays on indefinetely
				if ( (targetLED->duration != 0) && (currTime > targetLED->nextSwitch) )
				{
					targetLED->enabled = false;
					digitalWrite( targetLED->pin, LOW );    
				}
			}
			break;
		case LED_BLINK_LOOP:
			{
				if (currTime > targetLED->nextSwitch)
				{

					targetLED->pwr = !targetLED->pwr;

					if (targetLED->pwr)
					{
//						digitalWrite( targetLED->pin, HIGH );
						analogWrite( targetLED->pin, 128);  // PWM
						targetLED->nextSwitch = currTime + targetLED->duration;                
					}
					else
					{
						digitalWrite( targetLED->pin, LOW );
						targetLED->blinkIndex++;
						if (targetLED->blinkIndex >= targetLED->blinkCount)
						{
							targetLED->blinkIndex = 0;
							targetLED->nextSwitch = currTime + targetLED->interval;                
						} else
						{
							targetLED->nextSwitch = currTime + targetLED->multipleInterval;
						}
					}        
				}
			}
			break;
		default:
			break;
		}
	}
#endif
}

