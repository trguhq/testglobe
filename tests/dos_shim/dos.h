/* Host-only INT 33h recording interface; this does not emulate segmented DOS. */
#ifndef TEST_DOS_H
#define TEST_DOS_H
union REGS {
    struct { unsigned short ax, bx, cx, dx, si, di, cflag, flags; } x;
};
int int86(int interrupt_number, union REGS *input, union REGS *output);
void (*_dos_getvect(unsigned int interrupt_number))(void);
#endif
