/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

MEMORY
{
	FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1024K
	RAM  (rwx) : ORIGIN = 0x1FFF0000, LENGTH = 256K
	EXTERNAL_RAM (rwx) : ORIGIN = 0x70000000, LENGTH = 0x1000000 /* 16M bytes (alias RAM4) */  
}

__base_EXTERNAL_RAM = 0x70000000  ; /* EXTERNAL_RAM */  
__base_RAM4 = 0x70000000 ; /* RAM4 */  
__top_EXTERNAL_RAM = 0x70000000 + 0x1000000 ; /* 16M bytes */  
__top_RAM4 = 0x70000000 + 0x1000000 ; /* 16M bytes */  

SECTIONS
{
	.text : {
		. = 0;
		KEEP(*(.vectors))
		*(.startup*)
		/* TODO: does linker detect startup overflow onto flashconfig? */
		. = 0x400;
		KEEP(*(.flashconfig*))
		*(.text*)
		*(.audio_flash_functions*)
		*(.audio_ram_functions*)
		*(.rodata*)
		. = ALIGN(4);
		KEEP(*(.init))
		. = ALIGN(4);
		__preinit_array_start = .;
		KEEP (*(.preinit_array))
		__preinit_array_end = .;
		__init_array_start = .;
		KEEP (*(SORT(.init_array.*)))
		KEEP (*(.init_array))
		__init_array_end = .;
	} > FLASH = 0xFF

	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > FLASH
	_etext = .;

	.usbdescriptortable (NOLOAD) : {
		/* . = ORIGIN(RAM); */
		. = ALIGN(512);
		*(.usbdescriptortable*)
	} > RAM

	.dmabuffers (NOLOAD) : {
		. = ALIGN(4);
		*(.dmabuffers*)
	} > RAM

	.usbbuffers (NOLOAD) : {
		. = ALIGN(4);
		*(.usbbuffers*)
	} > RAM

	.data : AT (_etext) {
		. = ALIGN(4);
		_sdata = .; 
		*(.fastrun*)
		*(.data*)
		. = ALIGN(4);
		_edata = .; 
	} > RAM


    /* NOINIT section for EXTERNAL_RAM */
    .noinit_RAM4 (NOLOAD) : ALIGN(8)
    {       
       KEEP(*(.scratch_memory))
       . = ALIGN(4);
       KEEP(*(.delay_memory))
       . = ALIGN(4);
       KEEP(*(.reverb_memory))
       . = ALIGN(4);
       KEEP(*(.sample_memory))
       . = ALIGN(4);
       *(.noinit.$RAM4*)
       *(.noinit.$EXTERNAL_RAM*)
       . = ALIGN(8) ;
    } > EXTERNAL_RAM 


	.noinit (NOLOAD) : {
		*(.noinit*)
	} > RAM

	.bss_sram_l (NOLOAD) : {
		. = ALIGN(4);
		_sbss = .;
		__bss_start__ = .;
		*(EXCLUDE_FILE(*libTracker.a:*) .bss*)
		*(EXCLUDE_FILE(*libTracker.a:*) COMMON)
		. = ALIGN(4);
	} > RAM

	.bss 0x20000000 (NOLOAD) : {
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
		__bss_end = .;
		__bss_end__ = .;
	} > RAM


/* 	Reserve and place Heap within memory map 
    _HeapSize = 0x1000;
    .heap :  ALIGN(8)
    {
        _pvHeapStart = .;
        . += _HeapSize;
        . = ALIGN(8);
        _pvHeapLimit = .;
    } > RAM

     _StackSize = 0x1000;
      Reserve space in memory for Stack 
    .heap2stackfill  :
    {
        . += _StackSize;
    } > RAM
     Locate actual Stack in memory map 
    .stack ORIGIN(RAM) + LENGTH(RAM) - _StackSize - 0:  ALIGN(8)
    {
        _vStackBase = .;
        . = ALIGN(8);
        _vStackTop = . + _StackSize;
    } > RAM

*/
	
    /* Provide basic symbols giving location and size of main text
     * block, including initial values of RW data sections. Note that
     * these will need extending to give a complete picture with
     * complex images (e.g multiple Flash banks).
     */
     
     _estack = ORIGIN(RAM) + LENGTH(RAM);
	
     
    _image_start = LOADADDR(.text);
    _image_end = LOADADDR(.data) + SIZEOF(.data);
    _image_size = _image_end - _image_start;
}

ASSERT((ADDR(.bss_sram_l) + SIZEOF(.bss_sram_l)) <= 0x20000000, "Platform .bss overflowed SRAM_L 0x20000000 boundary");




