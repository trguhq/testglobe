/*
 * testglobe - host checks for the TI module appended to the DOS executable.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Included by drv_tga.c and the tga_pack.c build tool, never by tga_gsp.c:
 * the TMS340 compiler reads only tga_cmd.h and never sees this trailer.
 * Static C89 helpers keep the packer a single-source build.
 */
#ifndef tga_rlm_h
#define tga_rlm_h

/* Appended executable payload: eight magic bytes followed by little-endian
 * 32-bit base-image length, module length, IEEE CRC-32 and a reserved zero.
 * The DOS MZ image length remains unchanged, so this payload is read from
 * disk only when TI transforms are requested, not retained in DOS memory.
 */
#define TGA_RLM_MAGIC "TGRLM001"
#define TGA_RLM_TRAILER_SIZE 24U
#define TGA_RLM_MAX_SIZE 1048576UL
/* Every MZ field checked below lies in the 28-byte formatted header. */
#define TGA_RLM_MZ_HEADER 28U

#define TGA_RLM_U16(p) ((unsigned long)(p)[0] | ((unsigned long)(p)[1] << 8))
#define TGA_RLM_U32(p) (TGA_RLM_U16(p) | (TGA_RLM_U16((p) + 2) << 16))

/* DOS loads e_cp 512-byte pages, the last holding e_cblp bytes (zero means
 * a full page). Return that load-image length, or zero when DOS would
 * reject the header or the image would extend beyond limit bytes. */
static unsigned long tga_rlm_image_size(const unsigned char *header,
                                        unsigned long limit)
{
    unsigned long last, pages, image, header_size;
    if (header[0] != 'M' || header[1] != 'Z') return 0UL;
    last = TGA_RLM_U16(header + 2);
    pages = TGA_RLM_U16(header + 4);
    header_size = TGA_RLM_U16(header + 8) * 16UL;
    if (pages == 0UL || last > 511UL) return 0UL;
    image = (pages - 1UL) * 512UL + (last != 0UL ? last : 512UL);
    if (header_size < TGA_RLM_MZ_HEADER || header_size > image || image > limit)
        return 0UL;
    return image;
}

/* Reflected IEEE CRC-32 without a table: start at 0xffffffff and complement
 * the result. Bitwise updates are ample for a payload read once. */
static unsigned long tga_rlm_crc(unsigned long crc, const unsigned char *bytes,
                                 unsigned int count)
{
    unsigned int i, bit;
    for (i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1UL) != 0UL ? 0xedb88320UL : 0UL);
    }
    return crc;
}

#endif /* tga_rlm_h */
