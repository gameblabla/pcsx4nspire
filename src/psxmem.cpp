/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *   schultz.ryan@gmail.com, http://rschultz.ath.cx/code.php               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Steet, Fifth Floor, Boston, MA 02111-1307 USA.            *
 ***************************************************************************/

/*
* PSX memory functions.
*/

#include <sys/types.h>
#include <dirent.h>

#include "psxmem.h"
#include "r3000a.h"
#include "psxhw.h"
#ifndef WIN32
//#include <sys/mman.h>
#endif
#include "port.h"
#include "profiler.h"

#if defined(PSXREC) && defined(mips)
// For Posix shared mem:
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#endif

s8 *psxM = NULL;
s8 *psxP = NULL;
s8 *psxR = NULL;
s8 *psxH = NULL;

u8 **psxMemWLUT = NULL;
u8 **psxMemRLUT = NULL;
u8 *psxNULLread=NULL;

/*  Playstation Memory Map (from Playstation doc by Joshua Walker)
0x0000_0000-0x0000_ffff		Kernel (64K)	
0x0001_0000-0x001f_ffff		User Memory (1.9 Meg)	

0x1f00_0000-0x1f00_ffff		Parallel Port (64K)	

0x1f80_0000-0x1f80_03ff		Scratch Pad (1024 bytes)	

0x1f80_1000-0x1f80_2fff		Hardware Registers (8K)	

0x8000_0000-0x801f_ffff		Kernel and User Memory Mirror (2 Meg) Cached	

0xa000_0000-0xa01f_ffff		Kernel and User Memory Mirror (2 Meg) Uncached	

0xbfc0_0000-0xbfc7_ffff		BIOS (512K)
*/

int psxMemInit() {
	int i;

	psxMemRLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	psxMemWLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	memset(psxMemRLUT, 0, 0x10000 * sizeof(void *));
	memset(psxMemWLUT, 0, 0x10000 * sizeof(void *));

#if defined(PSXREC) && defined(mips)
	/* This is needed for direct writes in mips recompiler */

	// Allocate 64K each for 0x1f00_0000 and 0x1f80_0000 regions
	psxP = (s8 *)malloc(0x10000);
	psxH = (s8 *)malloc(0x10000);

	//NOTE: if your platform lacks POSIX shared memory, you could achieve the
	// same effects here by mmap()ing against a tmpfs, memfd or SysV shm file.

	bool shm_success = true;
	// Get a POSIX shared memory object fd
	int shfd = shm_open("/pcsx4all_psxmem", O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
	if (shfd == -1) {
		printf("Error acquiring POSIX shared memory file descriptor\n");
		shm_success = false;
	}

	// We want 2MB of PSX RAM
	if (shm_success && (ftruncate(shfd, 0x200000) == -1)) {
		printf("Error in call to ftruncate() on POSIX shared memory fd\n");
		shm_success = false;
	}

	// Map PSX RAM to start at fixed virtual address 0x1000_0000
	if (shm_success) {
		void* mmap_retval = mmap((void*)0x10000000, 0x200000,
				PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, shfd, 0);
		if (mmap_retval == MAP_FAILED || mmap_retval != (void*)0x10000000) {
			printf("Error: mmap() to 0x10000000 of POSIX shared memory fd failed\n");
			shm_success = false;
		} else {
			psxM = (s8*)mmap_retval;

			// Mirror upper 64KB PSX RAM to the fixed virtual region before psxM[].
			//  This allows recompiler to skip special-casing certain loads/stores
			//  of/to immediate($reg) address, where $reg is a value near a RAM
			//  mirror-region boundary and the immediate is a negative value large
			//  enough to cross to the region before it, i.e. (-16)(0x8020_0000).
			//  This occurs in games like Einhander. Normally, 0x8020_0000 on a PSX
			//  would be referencing the beginning of a 2MB mirror in the KSEG0 cached
			//  mirror region. After emu code masks the address, it maps to address
			//  0, and accessing -16(&psxM[0]) would be out-of-bounds.
			//  The mirror here maps it to &psxM[1ffff0], like a real PSX.

			mmap_retval = mmap((void*)((u8*)psxM-0x10000), 0x10000,
					PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, shfd, 0x200000-0x10000);
			if (mmap_retval == MAP_FAILED || mmap_retval != (void*)((u8*)psxM-0x10000)) {
				printf("Warning: creating lower mmap() mirror of POSIX shared memory fd failed\n");
				shm_success = false;
			}

			// And, for correctness's sake, mirror lower 64K of PSX RAM to region after
			//  psxM[], though in practice it's unknown if any games truly need this.
			mmap_retval = mmap((void*)((u8*)psxM+0x200000), 0x10000,
					PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, shfd, 0);
			if (mmap_retval == MAP_FAILED || mmap_retval != (void*)((u8*)psxM+0x200000)) {
				printf("Warning: creating upper mmap() mirror of POSIX shared memory fd failed\n");
				shm_success = false;
			}
		}
	}

	// Remove object.. backing RAM is released when munmap()'ed or pid terminates
	shm_unlink("/pcsx4all_psxmem");

	if (shm_success)
		printf("Mapped and mirrored PSX RAM using POSIX shared memory successfully.\n");
#else
	psxM = (s8 *)malloc(0x00220000);
	psxP = &psxM[0x200000];
	psxH = &psxM[0x210000];
#endif
	psxRegs.psxM=psxM;
	psxRegs.psxP=psxP;
	psxRegs.psxH=psxH;

	psxR = (s8 *)malloc(0x00080000);
	psxRegs.psxR=psxR;

	psxNULLread=(u8*)malloc(0x10000);
	memset(psxNULLread, 0, 0x10000);
	
	if (psxMemRLUT == NULL || psxMemWLUT == NULL || 
		psxM == NULL || psxP == NULL || psxH == NULL ||
		psxNULLread == NULL) {
		printf("Error allocating memory!");
		return -1;
	}

// MemR
	for (i = 0; i< 0x10000; i++) psxMemRLUT[i]=psxNULLread;
	for (i = 0; i < 0x80; i++) psxMemRLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];

	memcpy(psxMemRLUT + 0x8000, psxMemRLUT, 0x80 * sizeof(void *));
	memcpy(psxMemRLUT + 0xa000, psxMemRLUT, 0x80 * sizeof(void *));

	psxMemRLUT[0x1f00] = (u8 *)psxP;
	psxMemRLUT[0x1f80] = (u8 *)psxH;

	for (i = 0; i < 0x08; i++) psxMemRLUT[i + 0x1fc0] = (u8 *)&psxR[i << 16];

	memcpy(psxMemRLUT + 0x9fc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));
	memcpy(psxMemRLUT + 0xbfc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));

// MemW
	for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];
	memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
	memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));

	psxMemWLUT[0x1f00] = (u8 *)psxP;
	psxMemWLUT[0x1f80] = (u8 *)psxH;

	psxRegs.writeok = 1;

	return 0;
}

void psxMemReset() {
	DIR *dirstream = NULL;
	struct dirent *direntry;
	boolean biosfound = FALSE;
	FILE *f = NULL;
	char bios[MAXPATHLEN];

	memset(psxM, 0, 0x00200000);
	memset(psxP, 0, 0x00010000);
	memset(psxR, 0, 0x80000);    // Bios memory

	if (Config.HLE==FALSE) {
		dirstream = opendir(Config.BiosDir);
		if (dirstream == NULL) {
			printf("Could not open BIOS directory: \"%s\". Enabling HLE Bios!\n", Config.BiosDir);
			Config.HLE = TRUE;
			return;
		}

		while ((direntry = readdir(dirstream))) {
			if (!strcasecmp(direntry->d_name, Config.Bios)) {
				if (snprintf(bios, MAXPATHLEN, "%s/%s", Config.BiosDir, direntry->d_name) >= MAXPATHLEN)
					continue;

				f = fopen(bios, "rb");

				if (f == NULL) {
					continue;
				} else {
					size_t bytes_read, bytes_expected = 0x80000;
					bytes_read = fread(psxR, 1, bytes_expected, f);
					if (bytes_read == 0) {
						printf("Error: skipping empty BIOS file %s!\n", bios);
						fclose(f);
						continue;
					} else if (bytes_read < bytes_expected) {
						printf("Warning: size of BIOS file %s is smaller than expected!\n", bios);
						printf("Expected %zu bytes and got only %zu\n", bytes_expected, bytes_read);
					} else {
						printf("Loaded BIOS image: %s\n", bios);
					}

					fclose(f);
					Config.HLE = FALSE;
					biosfound = TRUE;
					break;
				}
			}
		}
		closedir(dirstream);

		if (!biosfound) {
			printf("Could not locate BIOS: \"%s\". Enabling HLE BIOS!\n", Config.Bios);
			Config.HLE = TRUE;
		}
	}

	if (Config.HLE)
		printf("Using HLE emulated BIOS functions. Expect incompatibilities.\n");
}

void psxMemShutdown() {
	free(psxNULLread);
#if defined(PSXREC) && defined(mips)
	munmap((void*)((u8*)psxM-0x10000), 0x10000);  // Unmap lower mirrored region
	munmap((void*)((u8*)psxM+0x200000), 0x10000); // Unmap upper mirrored region
	munmap((void*)psxM, 0x200000);
	// These are allocated separately to allow mirroring of psxM:
	free(psxH);
	free(psxP);
#else
	free(psxM);
#endif
	free(psxR);
	free(psxMemRLUT);
	free(psxMemWLUT);
}

u8 psxMemRead8(u32 mem) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead8++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
	u8 ret;
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalreads;
	extern unsigned long long totalreads_ok;
	totalreads++;
	if ((mem&0x1fffffff)<0x800000)
	{
		totalreads_ok++;
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemRead8");
			pcsx4all_exit();
		}
		ret=*((u8 *)&psxM[mem&0x1fffff]);
	}
	else
	{
#endif
		if (t!=0x1f80) ret= *(((u8 *)psxMemRLUT[t]) + m);
		else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_mh;
			totalreads_mh++;
#endif
			ret= (*(u8*) &psxH[m]);
		} else {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_hw;
			totalreads_hw++;
#endif
		       	ret=psxHwRead8(mem);
		}
#ifdef DEBUG_FAST_MEMORY
	}
#endif
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
	return ret;
}

u8 psxMemRead8_direct(u32 mem,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead8_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000)
				return (*(u8*) &regs->psxH[m]);
			return psxHwRead8(mem);
		case 0x1f00:
			return (*(u8*) &regs->psxP[m]);
		default:
			m|=mem&0x70000;
			return (*(u8*) &regs->psxR[m]);
	}
}

u16 psxMemRead16(u32 mem) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead16++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
	u16 ret;
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalreads;
	extern unsigned long long totalreads_ok;
	totalreads++;
	if ((mem&0x1fffffff)<0x800000)
	{
		totalreads_ok++;
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemRead16");
			pcsx4all_exit();
		}
		ret=*((u16 *)&psxM[mem&0x1fffff]);
	}
	else
	{
#endif
		if (t!=0x1f80) ret=SWAPu16(*(u16 *)(((u8 *)(psxMemRLUT[t])) + m));
		else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_mh;
			totalreads_mh++;
#endif
			ret=(SWAP16(*(u16*)&psxH[m]));
		} else {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_hw;
			totalreads_hw++;
#endif
		       	ret=psxHwRead16(mem);
		}
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
#ifdef DEBUG_FAST_MEMORY
	}
#endif
	return ret;
}

u16 psxMemRead16_direct(u32 mem,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead16_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000)
				return (*(u16*) &regs->psxH[m]);
			return psxHwRead16(mem);
		case 0x1f00:
			return (*(u16*) &regs->psxP[m]);
		default:
			m|=mem&0x70000;
			return (*(u16*) &regs->psxR[m]);
	}
}

u32 psxMemRead32(u32 mem) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead32++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
	u32 ret;
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalreads;
	extern unsigned long long totalreads_ok;
	totalreads++;
	if ((mem&0x1fffffff)<0x800000)
	{
		totalreads_ok++;
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemRead32");
			pcsx4all_exit();
		}
		ret=*((u32 *)&psxM[mem&0x1fffff]);
	}
	else
	{
#endif
		if (t!=0x1f80) ret=SWAPu32(*(u32 *)(((u8 *)(psxMemRLUT[t])) + m));
		else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_mh;
			totalreads_mh++;
#endif
			ret=(SWAP32(*(u32*)&psxH[m]));
		} else {
#ifdef DEBUG_FAST_MEMORY
			extern unsigned long long totalreads_hw;
			totalreads_hw++;
#endif
		       	ret=psxHwRead32(mem);
		}
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_READ, PCSX4ALL_PROF_CPU);
#ifdef DEBUG_FAST_MEMORY
	}
#endif
	return ret;
}

u32 psxMemRead32_direct(u32 mem,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemRead32_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000)
				return (*(u32*) &regs->psxH[m]);
			return psxHwRead32(mem);
		case 0x1f00:
			return (*(u32*) &regs->psxP[m]);
		default:
			m|=mem&0x70000;
			return (*(u32*) &regs->psxR[m]);
	}
}

void psxMemWrite8(u32 mem, u8 value) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite8++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalwrites;
	extern unsigned long long totalwrites_ok;
	totalwrites++;
	if ((mem&0x1fffffff)<0x800000)
	{
		totalwrites_ok++;
		if (!psxMemWLUT[t])
		{
			printf("FALLO psxMemWrite8 al ser 0 psxMemWLUT[%i] (%p)\n",t,mem);
			pcsx4all_exit();
		}
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemWrite8");
			pcsx4all_exit();
		}
		*((u8 *)&psxM[mem&0x1fffff]) = value;
		return;
	}
#endif
	if (t!=0x1f80)
	{
		u8 *p=(u8 *)(psxMemWLUT[t]);
		if (p)
		{
			*(p + m) = value;
			#ifdef PSXREC
			psxCpu->Clear((mem & (~3)), 1);
			#endif
		}
		#ifdef PSXMEM_LOG
		else PSXMEM_LOG("err sb %8.8lx\n", mem);
		#endif
	}
	else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_mh;
		totalwrites_mh++;
#endif
		(*(u8*) &psxH[m]) = value;
	} else {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_hw;
		totalwrites_hw++;
#endif
		psxHwWrite8(mem, value);
	}
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
}

void psxMemWrite8_direct(u32 mem, u8 value,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite8_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000) *((u8 *)&regs->psxH[m]) = value;
			else psxHwWrite8(mem,value);
			break;
		default:
			*((u8 *)&regs->psxP[m]) = value;
			break;
	}
}

void psxMemWrite16(u32 mem, u16 value) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite16++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalwrites;
	extern unsigned long long totalwrites_ok;
	totalwrites++;
	if ((mem&0x1fffffff)<0x800000)
	{
		totalwrites_ok++;
		if (!psxMemWLUT[t])
		{
			printf("FALLO psxMemWrite16 al ser 0 psxMemWLUT[%i] (%p)\n",t,mem);
			pcsx4all_exit();
		}
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemWrite16");
			pcsx4all_exit();
		}
		*((u16 *)&psxM[mem&0x1fffff]) = value;
		return;
	}
#endif
	if (t!=0x1f80)
	{
		u8 *p=(u8 *)(psxMemWLUT[t]);
		if (p)
		{
			*(u16 *)(p + m) = SWAPu16(value);
			#ifdef PSXREC
			psxCpu->Clear((mem & (~3)), 1);
			#endif
		}
		#ifdef PSXMEM_LOG
		else PSXMEM_LOG("err sh %8.8lx\n", mem);
		#endif
	}
	else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_mh;
		totalwrites_mh++;
#endif
		(*(u16*)&psxH[m]) = SWAPu16(value);
	} else {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_hw;
		totalwrites_hw++;
#endif
		psxHwWrite16(mem, value);
	}
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
}

void psxMemWrite16_direct(u32 mem, u16 value,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite16_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000) *((u16 *)&regs->psxH[m]) = value;
			else psxHwWrite16(mem,value);
			break;
		default:
			*((u16 *)&regs->psxP[m]) = value;
	}
}

void psxMemWrite32_error(u32 mem, u32 value) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite32_error++;
#endif
	static int writeok = 1;
	if (mem==0xfffe0130)
	{
		switch (value) {
			case 0x800: case 0x804:
				if (writeok == 0) break;
				psxRegs.writeok = writeok = 0;
				memset(psxMemWLUT + 0x0000, 0, 0x80 * sizeof(void *));
				memset(psxMemWLUT + 0x8000, 0, 0x80 * sizeof(void *));
				memset(psxMemWLUT + 0xa000, 0, 0x80 * sizeof(void *));
				break;
			case 0x00: case 0x1e988:
				if (writeok == 1) break;
				psxRegs.writeok = writeok = 1;
				{ int i; for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16]; }
				memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
				memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));
				break;
			default:
				#ifdef PSXMEM_LOG
				PSXMEM_LOG("unk %8.8lx = %x\n", mem, value);
				#endif
				break;
		}
	}
	#ifdef PSXREC
	else
	{
		if (!writeok) psxCpu->Clear(mem, 1);
		#ifdef PSXMEM_LOG
		if (writeok) { PSXMEM_LOG("err sw %8.8lx\n", mem); }
		#endif
	}
	#endif
}

void psxMemWrite32(u32 mem, u32 value) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite32++;
#endif
//	pcsx4all_prof_start_with_pause(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
	u32 t = mem >> 16;
	u32 m = mem & 0xffff;
#ifdef DEBUG_FAST_MEMORY
	extern unsigned long long totalwrites;
	extern unsigned long long totalwrites_ok;
	totalwrites++;
	if (((mem&0x1fffffff)<0x800000)&&(Config.HLE))
	{
		totalwrites_ok++;
		if (!psxMemWLUT[t])
		{
			printf("FALLO psxMemWrite32 al ser 0 psxMemWLUT[%i] (%p)\n",t,mem);
			pcsx4all_exit();
		}
		if ((((u8 *)psxMemRLUT[t]) + m) != (u8 *)&psxM[mem&0x1fffff])
		{
			printf("%p 0x%X 0x%X (0x%X 0x%X)\n",mem,t,m,m+t*0x10000,mem&0x1fffff);
			printf("%p %p %p\n",psxMemRLUT[t]+m,&psxM[m+t*0x10000],&psxM[mem&0x1fffff]);
			puts("FALLO psxMemWrite32");
			pcsx4all_exit();
		}
		*((u32 *)&psxM[mem&0x1fffff]) = value;
		return;
	}
#endif
	if (t!=0x1f80)
	{
		u8 *p=(u8 *)(psxMemWLUT[t]);
		if (p)
		{
			*(u32 *)(p + m) = SWAPu32(value);
			#ifdef PSXREC
			psxCpu->Clear(mem, 1);
			#endif
		}
		else
		{
			psxMemWrite32_error(mem,value);
		}
	}
	else if (m<0x1000) {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_mh;
		totalwrites_mh++;
#endif
		(*(u32*)&psxH[m]) = SWAPu32(value);
	} else {
#ifdef DEBUG_FAST_MEMORY
		extern unsigned long long totalwrites_hw;
		totalwrites_hw++;
#endif
		psxHwWrite32(mem, value);
	}
//	pcsx4all_prof_end_with_resume(PCSX4ALL_PROF_HW_WRITE, PCSX4ALL_PROF_CPU);
}

void psxMemWrite32_direct(u32 mem, u32 value,void *_regs) {
#ifdef DEBUG_ANALYSIS
	dbg_anacnt_psxMemWrite32_direct++;
#endif
	const psxRegisters *regs=(psxRegisters *)_regs;
	u32 m = mem & 0xffff;
	switch(mem>>16) {
		case 0x1f80:
			if (m<0x1000) *((u32 *)&regs->psxH[m]) = value;
			else psxHwWrite32(mem,value);
			break;
		default:
			*((u32 *)&regs->psxP[m]) = value;
	}
}

#if 0
void *psxMemPointer(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80) {
		if (mem < 0x1f801000)
			return (void *)&psxH[mem]; // WRONG???
		else
			return NULL;
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			return (void *)(p + (mem & 0xffff));
		}
		return NULL;
	}
}
#endif
