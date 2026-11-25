/* Host-only INT 33h recording interface; this does not emulate segmented DOS. */
#ifndef TEST_DOS_H
#define TEST_DOS_H
union REGS {
    struct { unsigned short ax, bx, cx, dx, si, di, cflag, flags; } x;
};
int int86(int interrupt_number, union REGS *input, union REGS *output);
void (*_dos_getvect(unsigned int interrupt_number))(void);
/* Interrupt-routine keywords compile away; tests call the routines directly. */
#ifndef _interrupt
#define _interrupt
#endif
#ifndef _far
#define _far
#endif
void _dos_setvect(unsigned int interrupt_number, void (*handler)());
/* Declared by <stdlib.h> in the DOS compilers. */
extern unsigned char _osmajor, _osminor;
#endif
