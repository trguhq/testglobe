/* Host recorder declarations from TI SPVU015C, Appendix A.
 * These describe API values, not the layout of a 16-bit Microsoft object. */
#ifndef TEST_TIGA_TYPEDEFS_H
#define TEST_TIGA_TYPEDEFS_H
typedef struct { unsigned char r, g, b, i; } PALET;
typedef struct { short maxwide, charhigh; } FONTINFO;
typedef struct {
    unsigned long disp_pitch;
    unsigned short disp_vres, disp_hres;
    short screen_wide, screen_high;
    unsigned short disp_psize;
    unsigned long pixel_mask;
    unsigned short palet_gun_depth;
    unsigned long palet_size;
    short palet_inset;
    unsigned short num_pages, num_offscrn_areas;
    unsigned long wksp_addr, wksp_pitch;
    unsigned short silicon_capability, color_class;
    unsigned long red_mask, blue_mask, green_mask;
    unsigned short x_aspect, y_aspect, diagonal_aspect;
} MODEINFO;
typedef struct {
    unsigned short version_number;
    unsigned long comm_buff_size, sys_flags, device_rev;
    unsigned short num_modes, current_mode;
    unsigned long program_mem_start, program_mem_end;
    unsigned long display_mem_start, display_mem_end;
    unsigned long stack_size, shared_mem_size;
    void *shared_host_addr;
    unsigned long shared_gsp_addr;
    MODEINFO mode;
} CONFIG;
#endif
