/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0;

    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :code

    . = 0x100000;

    .data : {
        __data_start = .;
        *(.got)
        *(.got.plt)
        *(.data.rel.ro)
        *(.data.rel.ro.*)
        *(.data)
        *(.data.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __data_end = .;
    } :data

    /* Keep relocations as a named section */
    .relocs : {
        __relocs_start = .;
        *(.rela.dyn)
        *(.rela.*)
        __relocs_end = .;
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.gnu.hash)
        *(.gnu.version*)
        *(.plt)
        *(.plt.*)
        *(.rel.*)
    }
}
