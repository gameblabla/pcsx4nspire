#include <libndls.h>
#include <keys.h>
#include <nucleus.h>
#include <ngc.h>
#include <syscall-decls.h>
#include "n2DLib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*             *
 *  Buffering  * 
*/
scr_type_t screen_type;

unsigned short *BUFF_BASE_ADDRESS, *ALT_SCREEN_BASE_ADDRESS, *INV_BUFF, *temp;
void *SCREEN_BACKUP;
int swapped = 0;
unsigned char new_rev;

void initBuffering()
{
	screen_type = lcd_type();
	lcd_init(screen_type);
	
	if (screen_type == SCR_240x320_565)
	{
		new_rev = 1;
	}
	else
	{
		new_rev = 0;
	}
	
	BUFF_BASE_ADDRESS = (unsigned short*)malloc(BUFF_BYTES_SIZE);
	if(!BUFF_BASE_ADDRESS) exit(0);
	
	SCREEN_BACKUP = *(void**)0xC0000010;
	
	// Handle monochrome screens-specific shit
	if(is_classic)
		*(int32_t*)(0xC000001C) = (*((int32_t*)0xC000001C) & ~0x0e) | 0x08;
	
	ALT_SCREEN_BASE_ADDRESS = (unsigned short*)malloc(BUFF_BYTES_SIZE + 8);
	if(!ALT_SCREEN_BASE_ADDRESS)
	{
		free(BUFF_BASE_ADDRESS);
		*((int32_t*)0xC000001C) = (*((int32_t*)0xC000001C) & ~0x0e) | 0x04;
		*(void**)0xC0000010 = SCREEN_BACKUP;
		exit(0);
	}
	
	INV_BUFF = (unsigned short*)malloc(BUFF_BYTES_SIZE);
	if(!INV_BUFF)
	{
		free(ALT_SCREEN_BASE_ADDRESS);
		free(BUFF_BASE_ADDRESS);
		*((int32_t*)0xC000001C) = (*((int32_t*)0xC000001C) & ~0x0e) | 0x04;
		*(void**)0xC0000010 = SCREEN_BACKUP;
		exit(0);
	}
	
	*(void**)0xC0000010 = ALT_SCREEN_BASE_ADDRESS;
}

void updateScreen()
{
	unsigned int *dest, *src, i, c;
	// I use different methods for refreshing the screen for GS and color screens because according to my tests, the fastest for one isn't the fastest for the other
		if (has_colors)
		{
			lcd_blit(BUFF_BASE_ADDRESS, screen_type);
			/*dest = (unsigned int*)ALT_SCREEN_BASE_ADDRESS;
			src = (unsigned int*)BUFF_BASE_ADDRESS;
			for(i = 0; i < 160 * 240; i++)
				*dest++ = *src++;*/
		}
		else
		{
			dest = (unsigned int*)INV_BUFF;
			src = (unsigned int*)BUFF_BASE_ADDRESS;
			for(i = 0; i < 38400/*160 * 240*/; i++)
			{
				c = *src++;
				c = ~c;
				// c holds two 16-bits colors, decompose them while keeping them that way
				*dest++ = ((c & 0x1f) + (((c >> 5) & 0x3f) >> 1) + ((c >> 11) & 0x1f)) / 3
					+ ((((c >> 16) & 0x1f) + (((c >> 21) & 0x3f) >> 1) + ((c >> 27) & 0x1f)) / 3 << 16);
				
			}
			
			temp = *(void**)0xC0000010;
			*(void**)0xC0000010 = INV_BUFF;
			INV_BUFF = temp;
			swapped = !swapped;
		}
}

void deinitBuffering()
{
	// Handle monochrome screens-specific shit again
	if(is_classic)
		*((int32_t*)0xC000001C) = (*((int32_t*)0xC000001C) & ~0x0e) | 0x04;
	*(void**)(0xC0000010) = SCREEN_BACKUP;
	if(swapped)
	{
		temp = *(void**)0xC0000010;
		*(void**)0xC0000010 = INV_BUFF;
		INV_BUFF = temp;
	}
	free(INV_BUFF);
	free(ALT_SCREEN_BASE_ADDRESS);
	free(BUFF_BASE_ADDRESS);
}

/*                 *
 * Hardware timers *
 *                 */
// Everything on HW timers is by aeTIos and Streetwalrus from http://www.omnimaga.org/

#define TIMER 0x900D0000

/*         *
 *  Maths  *
 *         */

 /*
Example:
2.5 * 3.5 :
	xdec = 128
	ydec = 128
	xint = 2
	yint = 3
2.5 * 3 = 8.75 :
	rdec = 192
	rint = 8
*/
 

/*            *
 *  Graphics  *
 *            */

void clearBufferB()
{
	int i;
	for(i = 0; i < 38400; i++)
		((unsigned int*)BUFF_BASE_ADDRESS)[i] = 0;
}

inline unsigned short getPixel(const unsigned short *src, unsigned int x, unsigned int y)
{
	if(x < src[0] && y < src[1])
		return src[x + y * src[0] + 3];
	else
		return src[2];
}

inline void setPixelUnsafe(unsigned int x, unsigned int y, unsigned short c)
{
	switch(new_rev)
	{
		case 0:
			*((unsigned short*)BUFF_BASE_ADDRESS + x + y * 320) = c;
		break;
		case 1:
			*((unsigned short*)BUFF_BASE_ADDRESS + y + x * 240) = c;
		break;
	}
}


inline void setPixel(unsigned int x, unsigned int y, unsigned short c)
{
	if(x < 320 && y < 240)
	{
		switch(new_rev)
		{
			case 0:
				*((unsigned short*)BUFF_BASE_ADDRESS + x + y * 320) = c;
			break;
			case 1:
				*((unsigned short*)BUFF_BASE_ADDRESS + y + x * 240) = c;
			break;
		}
	}
}

#ifdef __cplusplus
}
#endif
