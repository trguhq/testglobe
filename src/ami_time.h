/*
 * testglobe - timer.device clock shared by the Amiga chipset and MiniGL targets.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ami_time_h
#define ami_time_h

/* Open after intuition.library and close before it; the clock falls back
 * to Intuition's CurrentTime while timer.device is unavailable. Opening
 * again first closes the previous request. */
extern void ami_clock_open(void);
extern void ami_clock_close(void);
/* Elapsed seconds from a fixed source. It never decreases; a backward
 * system-time adjustment rebases without freezing progress. */
extern double ami_clock_now(void);

#endif /* ami_time_h */
