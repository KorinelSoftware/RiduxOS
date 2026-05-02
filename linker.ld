ENTRY(_start)

SECTIONS
{
  . = 1M;

  .text ALIGN(4K) :
  {
    KEEP(*(.multiboot))
    *(.text*)
  }

  .rodata ALIGN(4K) :
  {
    *(.rodata*)
    *(.lrodata*)
  }

  .data ALIGN(4K) :
  {
    *(.data*)
  }

  .bss ALIGN(4K) :
  {
    *(COMMON)
    *(.bss*)
    *(.lbss*)
  }

  . = ALIGN(4K);
  __kernel_end = .;
  _kernel_end = .;
  end = .;
  _end = .;

  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
    *(.note*)
  }
}
