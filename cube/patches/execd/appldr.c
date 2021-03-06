/*
appldr.c for Swiss
Copyright (C) 2015 FIX94
*/
#include "../../reservedarea.h"
#include "../base/common.h"

typedef int   (*app_main)(char **dst, u32 *size, u32 *offset);
typedef void  (*app_init)(int (*report)(const char *fmt, ...),u32 args);
typedef void *(*app_final)();
typedef void  (*app_entry)(void (**init)(), int (**main)(), void *(**final)());
typedef void  (*_device_frag_read)(void *dst, u32 len, u32 offset);

/* Thanks Tinyload */
typedef struct _apploader_hdr
{
	char revision[16];
	void *entry;
	s32 size;
	s32 trailersize;
	s32 padding;
} apploader_hdr;

int noprintf( const char *str, ... ) { return 0; };

void memset32(u32 dst,u32 fill,u32 len)
{
	u32 i;
	for(i = 0; i < len; i+=4)
		*((vu32*)(dst+i)) = fill;
}

void *memcpy(void *dest, const void *src, u32 size)
{
	char *d = dest;
	const char *s = src;

	while (size--)
		*d++ = *s++;

	return dest;
}

#define ALIGN_FORWARD(x,align) \
	((typeof(x))((((u32)(x)) + (align) - 1) & (~(align-1))))

void memset32(u32 dst,u32 fill,u32 len);

u32 RunAppldr(void *fragReadPtr)
{
	_device_frag_read device_frag_read = fragReadPtr;
	/* Low Mem Clearing */
	memset32(0x80000040,0,0x8C);
	memset32(0x800000D4,0,0x1C);
	*(vu32*)0x800000F4 = 0;
	memset32(0x80003000,0,0xC0);
	memset32(0x800030C8,0,0xC);

	/* added cause no OSInit */
	memset32(0x800030DC,0,0x24);

	memset32(VAR_DI_REGS,0,0x24);
	memset32(VAR_STREAM_START,0,0xA0);

	/* Stop Interrupts */
	*(vu32*)VAR_FAKE_IRQ_SET = 0;
	*(vu32*)0xCC006000 = 0x7E;

	app_entry appldr_entry;
	app_init  appldr_init;
	app_main  appldr_main;
	app_final appldr_final;

	char *dst;
	u32 len, offset;

	apploader_hdr appldr_hdr;

	/* Read apploader header */
	u32 appldr_loc = *((vu32*)0x812FCFF4);
	device_frag_read(&appldr_hdr, 0x20, appldr_loc);

	/* Calculate apploader length */
	u32 appldr_len = ALIGN_FORWARD(appldr_hdr.size + appldr_hdr.trailersize, 0x20);

	/* Read apploader code */
	device_frag_read((void*)0x81200000, appldr_len, appldr_loc + 0x20);

	/* Flush into memory */
	dcache_flush_icache_inv((void*)0x81200000, appldr_len);

	/* Set apploader entry function */
	appldr_entry = appldr_hdr.entry;

	/* Call apploader entry */
	appldr_entry(&appldr_init, &appldr_main, &appldr_final);

	/* Initialize apploader */
	u32 DolOffset = *((vu32*)0x812FCFF8);
	appldr_init(noprintf, DolOffset);

	/* Read DOL */
	while(appldr_main(&dst, &len, &offset))
	{
		device_frag_read(dst, len, offset);
		dcache_store(dst, len);
	}

	/* Copy BI2 */
	u32 fstloc = *((vu32*)0x80000038);
	u32 bi2 = fstloc - 0x2000;
	memcpy((void*)bi2, (void*)0x812FD000, 0x2000);
	*((vu32*)0x800000F4) = bi2;

	/* ??? */
	*((vu32*)(bi2+0x20)) = 1;
	*((vu32*)(bi2+0x24)) = 5;

	/* Update Memory Pointers */
	*((vu32*)0x80000030) = 0;
	*((vu32*)0x80000034) = fstloc;

	/* Set entry point from apploader */
	return (u32)(appldr_final());
}
