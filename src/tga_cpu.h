/*
 * testglobe - real-mode DOS processor queries for the TIGA driver.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef tga_cpu_h
#define tga_cpu_h

/* 486 means 80486 or later. These queries do not require CPUID or a DOS
 * extender. An unrecognized older processor returns the safe 8086 class. */
extern int tga_cpu_detect(void);
/* Conservative hardware test: a missing BIOS equipment flag selects the
 * integer path. NO87 also disables hardware transforms intentionally. */
extern int tga_cpu_has_fpu(void);

#endif /* tga_cpu_h */
