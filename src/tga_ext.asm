; testglobe - TIGA 2.0 downloadable extension table.
; Copyright (C) 2026 trguhq
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Assemble with TI gspcl/gspa, not the x86 Microsoft assembler.
; Position zero must match TGA_GSP_TRANSFORM in tga_cmd.h.
        .globl  _tga_gsp_transform
        .sect   ".TIGAEXT"
        .long   _tga_gsp_transform
        .text
