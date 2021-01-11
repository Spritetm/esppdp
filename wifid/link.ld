/* Copyright (C) 2014-2020 Free Software Foundation, Inc.
   Copying and distribution of this script, with or without modification,
   are permitted in any medium without royalty provided the copyright
   notice and this notice are preserved.  */
OUTPUT_FORMAT("a.out-pdp11", "a.out-pdp11",
	      "a.out-pdp11")
OUTPUT_ARCH(pdp11)
SEARCH_DIR("/home/jeroen/esp8266/esp32/esp32-pdp11/gcc/binutils-2.35.1/crosstool/pdp11-aout/lib");
PROVIDE (__stack = 0);
SECTIONS
{
  .text 0x0 : AT (0)
  {
    CREATE_OBJECT_SYMBOLS
    *(.text)
    _etext = .;
    __etext = .;
  }

  .data 0x0: AT (0+SIZEOF(.text))
  {
    *(.data)
    CONSTRUCTORS
    _edata  =  .;
    __edata  =  .;
  }
  .bss :
  {
    __bss_start = .;
   *(.bss)
   *(COMMON)
   . = ALIGN(2);
   _end = . ;
   __end = . ;
  }
}
