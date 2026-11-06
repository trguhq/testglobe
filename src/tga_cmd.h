/*
 * testglobe - TIGA host/TMS340 transform packet.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef tga_cmd_h
#define tga_cmd_h

/* Appended executable payload: eight magic bytes followed by little-endian
 * 32-bit base-image length, module length, IEEE CRC-32 and a reserved zero.
 * The DOS MZ image length remains unchanged, so this payload is read from
 * disk only when TI transforms are requested, not retained in DOS memory.
 */
#define TGA_RLM_MAGIC "TGRLM001"
#define TGA_RLM_TRAILER_SIZE 24U
#define TGA_RLM_MAX_SIZE 1048576UL

/* A packet consists of 32-bit longs, marshalled by TIGA's _ALTDWORD_PTR.
 * No host pointers, compiler structs, floats or native int widths cross the
 * processor boundary. The first three vertex words contain IEEE binary32
 * object coordinates on entry and signed Q4 camera coordinates on return.
 * The module also returns projected integer pixels and a validity word;
 * depth-crossing polygons use the host's existing clipping path instead.
 * Matrix preparation, polygon clipping/culling and submission remain on the
 * host; rotation, view translation and ordinary projection execute on TMS340.
 */
#define TGA_GSP_TRANSFORM 0
#define TGA_GSP_ABI 0x54474131L
#define TGA_GSP_MAGIC 0U
#define TGA_GSP_STATUS 1U
#define TGA_GSP_COUNT 2U
#define TGA_GSP_WIDTH 3U
#define TGA_GSP_HEIGHT 4U
#define TGA_GSP_FOCAL 5U
#define TGA_GSP_VIEW 6U
#define TGA_GSP_NEAR 7U
#define TGA_GSP_FAR 8U
#define TGA_GSP_MATRIX 9U
#define TGA_GSP_HEADER 18U
#define TGA_GSP_VERTEX_WORDS 6U
#define TGA_GSP_MAX_VERTICES 130U
#define TGA_GSP_WORDS(n) (TGA_GSP_HEADER + (n) * TGA_GSP_VERTEX_WORDS)
#define TGA_GSP_MAX_WORDS TGA_GSP_WORDS(TGA_GSP_MAX_VERTICES)

#define TGA_GSP_X 0U
#define TGA_GSP_Y 1U
#define TGA_GSP_Z 2U
#define TGA_GSP_SCREEN_X 3U
#define TGA_GSP_SCREEN_Y 4U
#define TGA_GSP_PROJECTED 5U

#define TGA_GSP_PENDING 0L
#define TGA_GSP_OK 1L
#define TGA_GSP_BAD_PACKET (-1L)
#define TGA_GSP_BAD_RANGE (-2L)

/* Includes ample allowance for command and both C-packet descriptors.
 * Check the queried comm_buff_size before calling the nonallocating cp_alt.
 */
#define TGA_GSP_PACKET_OVERHEAD 64UL
#define TGA_GSP_PACKET_BYTES(n) (4UL * TGA_GSP_WORDS(n) + TGA_GSP_PACKET_OVERHEAD)

/* Native module entry, also compiled on the host by the contract tests.
 * DOS application code invokes command zero through cp_alt, not this symbol.
 */
extern void tga_gsp_transform(unsigned long words, long *packet);

#endif /* tga_cmd_h */
