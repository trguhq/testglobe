/* Production OpenGL driver and GLUT adapter with public GL/GLUT call records.
 * Native geometry, format probing, glyph lists and pixel packing are tested;
 * no visual or GPU is emulated. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earth_reference.h"
#include "testglob.h"
#include "types.h"
#include "tex_ext.h"
#include "texsoft.h"
static Drv_result test_texture_build(int size);
static Drv_result test_texsoft_texture_take_filtered(unsigned int width,unsigned int height,unsigned char *rgb,int filter);
static int test_texsoft_begin(unsigned int width,unsigned int height,float model[4][4],float projection[4][4],int indexed,const unsigned char *colormap);
static unsigned char *test_texsoft_index_pixels(void);
static void test_texsoft_triangle(const Vertex *a,const Vertex *b,const Vertex *c,const Coord *ua,const Coord *ub,const Coord *uc);
#define texture_build test_texture_build
#define texsoft_texture_take_filtered test_texsoft_texture_take_filtered
#define texsoft_begin test_texsoft_begin
#define texsoft_index_pixels test_texsoft_index_pixels
#define texsoft_triangle test_texsoft_triangle
#include "../src/drv_ogl.c"
#include "../src/ogl_glut.c"
#undef texsoft_triangle
#undef texsoft_index_pixels
#undef texsoft_begin
#undef texsoft_texture_take_filtered
#undef texture_build
#include "texsoft.h"
extern Drv_result texture_build(int size);
extern Drv_result texsoft_texture_take_filtered(unsigned int width,unsigned int height,unsigned char *rgb,int filter);
extern int texsoft_begin(unsigned int width,unsigned int height,float model[4][4],float projection[4][4],int indexed,const unsigned char *colormap);

#define TEST_PI 3.14159265358979323846
#if GLUT_API_VERSION >= 4 || (defined(GLUT_XLIB_IMPLEMENTATION) && GLUT_XLIB_IMPLEMENTATION >= 9)
#define TEST_DISPLAY_STRINGS
#endif
/* Recorder names for 256-entry glyph blocks; globe lists stay below 64. */
#define GLYPH_BASE 1024U
#define GLYPH_BLOCKS 8U

extern Vertex *globe_verts;
extern int globe_longitude,globe_latitude;

static const char *profile;
static unsigned int requested_mode;
static unsigned int display_mode_calls,display_string_calls,mode_queries;
static int initial_x,initial_y;
static int active_window, next_window=1, window_count, destroy_count;
static unsigned int probe_windows,probe_destroys,final_windows;
static unsigned int probe_callbacks,probe_redraws,initial_window_redraws;
#ifndef OGL_GLUT_STRINGS
static unsigned int probed_modes[32],probed_count;
#endif
static struct Test_window {
    int live,indexed,doubled,red,green,blue,alpha,depth,width,height,probe;
    void (*display)(void);
} windows[64];
static int output_width=64,output_height=48;
static unsigned int palette_calls,draw_pixels_calls,texture_uploads,indices,rgb_calls,swaps,flushes;
static unsigned int direct_texture_uploads,capacity_queries;
static GLint texture_min_filter,texture_mag_filter;
static unsigned int texture_allocations,texture_deletions;

static unsigned int staging_attempts,soft_adoptions,soft_frame_attempts,pixel_attempts;
static unsigned long soft_triangles;
static int fail_traversal;
static unsigned int texture_exists,texture_binding,frame_texture_pixels;
static int texture_injection=1;
static unsigned int list_attempts,list_deletions,list_calls,list_validations;
static unsigned int frame_list_successes;
static int fail_next_call;
static GLenum pending_error,following_error;
static unsigned int error_events;
static unsigned int get_error_calls,is_texture_calls,binding_queries,enabled_queries,matrix_queries,float_queries;
static unsigned int maximum_texture_size=512;
static unsigned int emitted_vertices,texture_coordinates;
static unsigned int matrix_mode=GL_MODELVIEW,stack_depth[2];
static GLfloat matrices[2][16][16];
static GLenum begin_mode;
static struct Test_geometry {
    unsigned long vertices,triangles,cap_batches,strip_batches,textured_vertices;
} frame_geometry,list_geometry[64];
static unsigned int current_list,list_exists[64],list_reserved[64],block_vertices,block_coordinates;
static unsigned int list_compile_failed[64];
static unsigned int block_colors,redundant_colors;
static GLfloat block_polygon[3][3],block_uvs[3][2],first_uvs[2][2],last_uv[2];
static int texture_enabled,depth_enabled,cull_enabled,unpack_alignment=4;
static GLubyte last_index,last_rgb[3];
static int last_clear_index,index_shift,index_offset,map_color;
static GLfloat zoom_x=1,zoom_y=1;
static unsigned int native_lists,redisplay_posts,frame_clears;
static unsigned int posts_before_loop,loop_calls;
static struct Test_attrib {GLbitfield mask;int texture,depth,cull,shift,offset,map;GLfloat x,y;GLubyte index,rgb[3];GLuint base;} attribs[16];
static unsigned int attrib_depth,client_depth;
static int client_alignment[16];
static unsigned int palette_mask[256];
static void (*reshape_callback)(int,int);
static void (*mouse_callback)(int,int,int,int);
static void (*motion_callback)(int,int);
static void (*keyboard_callback)(unsigned char,int,int);
static char osd_lines[2][256];
static unsigned int osd_lengths[2],osd_half_rows[2];
static int osd_line=-1;
static unsigned int osd_rows;
static int osd_row_width;
static struct Glyph_block {int live;void *font;int chars[256];} glyph_blocks[GLYPH_BLOCKS];
static int glyph_compiling=-1,glyph_char;
static GLuint list_base;
static unsigned int glyph_block_attempts,glyph_block_builds,glyph_block_deletions;
static unsigned int direct_glyphs,call_lists_calls,bitmap_width_calls;

static int texture_profile(const char *name)
{return texture_injection&&strcmp(profile,name)==0;}
static int software_profile(void)
{return strcmp(profile,"indexed")==0||strncmp(profile,"soft",4)==0;}
static int shallow_rgb_profile(void)
{return strcmp(profile,"rgb8")==0||strcmp(profile,"rgb8only")==0;}
static int glyph_failure_profile(void)
{return strcmp(profile,"listfail")==0||strncmp(profile,"font",4)==0;}
static void inject(GLenum code)
{if(pending_error==GL_NO_ERROR)pending_error=code;else following_error=code;++error_events;}
static Drv_result test_texture_build(int size)
{++staging_attempts;if(texture_profile("stagefail")||((strcmp(profile,"reduced")==0||strcmp(profile,"softreduced")==0)&&size>128))return DRV_NO_MEMORY;return texture_build(size);}
static Drv_result test_texsoft_texture_take_filtered(unsigned int w,unsigned int h,unsigned char *rgb,int filter)
{++soft_adoptions;assert(filter==TEXSOFT_BILINEAR);
 if(texture_profile("softadoptfail")||(strcmp(profile,"softadoptreduced")==0&&w>128U))
 {free(rgb);return DRV_NO_MEMORY;}
 return texsoft_texture_take_filtered(w,h,rgb,filter);}
static int test_texsoft_begin(unsigned int w,unsigned int h,float m[4][4],float p[4][4],int indexed,const unsigned char *c)
{++soft_frame_attempts;return texture_profile("softframefail")?FALSE:texsoft_begin(w,h,m,p,indexed,c);}
static unsigned char *test_texsoft_index_pixels(void)
{++pixel_attempts;return texture_profile("softpixelsfail")?NULL:texsoft_index_pixels();}
static void test_texsoft_triangle(const Vertex *a,const Vertex *b,const Vertex *c,const Coord *ua,const Coord *ub,const Coord *uc)
{++soft_triangles;texsoft_triangle(a,b,c,ua,ub,uc);}

static void matrix_identity(GLfloat *m)
{int i;for(i=0;i<16;++i)m[i]=(GLfloat)(i%5==0);}
static unsigned int matrix_slot(void){return matrix_mode==GL_PROJECTION?1U:0U;}
static GLfloat *current_matrix(void){unsigned int s=matrix_slot();return matrices[s][stack_depth[s]];}
static void postmultiply(const GLfloat *b)
{
    GLfloat result[16],*a;
    int row,col,k;
    a=current_matrix();
    for(col=0;col<4;++col)for(row=0;row<4;++row){result[col*4+row]=0;for(k=0;k<4;++k)result[col*4+row]+=a[k*4+row]*b[col*4+k];}
    memcpy(a,result,sizeof(result));
}

static void geometry_add(struct Test_geometry *to,const struct Test_geometry *from)
{
    to->vertices+=from->vertices;to->triangles+=from->triangles;
    to->cap_batches+=from->cap_batches;to->strip_batches+=from->strip_batches;
    to->textured_vertices+=from->textured_vertices;
}

static unsigned char *earth_bytes(void)
{
    switch(globe_longitude){
    case 32:return test_earth_data(32);case 64:return test_earth_data(64);
    case 128:return test_earth_data(128);case 256:return test_earth_data(256);
    case 512:return test_earth_data(512);
    }
    assert(!"Unknown globe resolution");return NULL;
}

/* Two Earth palette entries may share one RGB value; only then may an RGB
 * window receive the same color twice in a row. */
static int rgb_shared(GLubyte r,GLubyte g,GLubyte b)
{
    unsigned int i,count=0;
    for(i=0;i<256;++i)if(earth_cmap[i][0]==r&&earth_cmap[i][1]==g&&earth_cmap[i][2]==b)++count;
    return count>1;
}

/* Reconstruct OpenGL's alternating strip winding and check the flat-shaded
 * color on the newly submitted (provoking) vertex against its Earth cell. */
static void check_triangle(const GLfloat *a,const GLfloat *b,const GLfloat *c)
{
    double ux,uy,uz,vx,vy,vz,cx,cy,cz,area,phi,theta;
    unsigned int latitude,longitude,index;
    ux=b[0]-a[0];uy=b[1]-a[1];uz=b[2]-a[2];
    vx=c[0]-a[0];vy=c[1]-a[1];vz=c[2]-a[2];
    cx=(a[0]+b[0]+c[0])/3.0;cy=(a[1]+b[1]+c[1])/3.0;
    cz=(a[2]+b[2]+c[2])/3.0;
    area=(uy*vz-uz*vy)*cx+(uz*vx-ux*vz)*cy+(ux*vy-uy*vx)*cz;
    assert(area>0.0);
    if(!texture_enabled){
        phi=atan2(cy,cx);if(phi<0)phi+=2.0*TEST_PI;
        theta=atan2(sqrt(cx*cx+cy*cy),cz);
        longitude=(unsigned int)(phi*globe_longitude/(2.0*TEST_PI));
        latitude=(unsigned int)(theta*globe_latitude/TEST_PI);
        assert(longitude<(unsigned int)globe_longitude&&latitude<(unsigned int)globe_latitude);
        index=earth_bytes()[(unsigned long)latitude*globe_longitude+longitude];
        if(windows[active_window].indexed)assert(last_index==index);
        else assert(last_rgb[0]==earth_cmap[index][0]&&last_rgb[1]==earth_cmap[index][1]&&last_rgb[2]==earth_cmap[index][2]);
    }
}

static int available(unsigned int request)
{
    int indexed=(request&GLUT_INDEX)!=0,alpha=(request&GLUT_ALPHA)!=0,doubled=(request&GLUT_DOUBLE)!=0;
    if(strcmp(profile,"nodepth")==0&&(request&GLUT_DEPTH))return 0;
    if(software_profile())return indexed;
    if(strcmp(profile,"rgb8only")==0)return !indexed&&!alpha;
    if(indexed)return 1;
    if((strcmp(profile,"rgb24")==0||strcmp(profile,"rgb16")==0||strcmp(profile,"rgb8")==0)&&alpha)return 0;
    if(strcmp(profile,"rgba-single")==0&&alpha&&doubled)return 0;
    return 1;
}

/* A native window operation may process its pending initial redraw before
 * GLUT's main loop. Every window owns its callback; it does not inherit the
 * previous window's callback. Probe/setup callbacks must not render a globe. */
static void initial_redraw(void)
{
    struct Test_window *w=&windows[active_window];
    unsigned int old_clears,old_lists,old_vertices,old_swaps,old_flushes;
    assert(w->live&&w->display!=NULL&&w->display!=drv_render);
    assert(globe_verts==NULL);
    old_clears=frame_clears;old_lists=native_lists;old_vertices=emitted_vertices;
    old_swaps=swaps;old_flushes=flushes;
    w->display();
    assert(frame_clears==old_clears&&native_lists==old_lists&&
           emitted_vertices==old_vertices&&swaps==old_swaps&&flushes==old_flushes);
    if(w->probe)++probe_redraws;else ++initial_window_redraws;
}

void glutInit(int *argc,char **argv){assert(argc&&argv);}
void glutInitDisplayMode(unsigned int mode){requested_mode=mode;++display_mode_calls;}
#ifdef TEST_DISPLAY_STRINGS
void glutInitDisplayString(const char *s)
{
    ++display_string_calls;
    requested_mode=0;
    if(strstr(s,"depth"))requested_mode|=GLUT_DEPTH;
    if(strstr(s,"index"))requested_mode|=GLUT_INDEX;
    if(strstr(s,"double"))requested_mode|=GLUT_DOUBLE;
    if(strstr(s,"alpha"))requested_mode|=GLUT_ALPHA;
#ifndef TEST_OGL_FREEGLUT
    /* Original GLUT gives bare rgba an alpha>=1 requirement. FreeGLUT
     * instead treats it like the GLUT_RGBA mode without requesting alpha. */
    if(strstr(s,"rgba"))requested_mode|=GLUT_ALPHA;
#endif
}
#endif
int glutGet(GLenum query)
{
    struct Test_window *w=&windows[active_window];
#ifdef GLUT_DISPLAY_MODE_POSSIBLE
    if(query==GLUT_DISPLAY_MODE_POSSIBLE){++mode_queries;return available(requested_mode);}
#endif
    assert(active_window>0&&w->live);
    switch(query){
    case GLUT_WINDOW_RGBA:return !w->indexed;
    case GLUT_WINDOW_DOUBLEBUFFER:return w->doubled;
    case GLUT_WINDOW_RED_SIZE:return w->red;
    case GLUT_WINDOW_GREEN_SIZE:return w->green;
    case GLUT_WINDOW_BLUE_SIZE:return w->blue;
    case GLUT_WINDOW_ALPHA_SIZE:return w->alpha;
    case GLUT_WINDOW_DEPTH_SIZE:return w->depth;
    case GLUT_WINDOW_BUFFER_SIZE:return w->indexed?8:(w->alpha<0?32:w->red+w->green+w->blue+w->alpha);
    case GLUT_WINDOW_COLORMAP_SIZE:return w->indexed?256:0;
    default:assert(!"Unmodeled GLUT query");return 0;
    }
}
void glutInitWindowSize(int w,int h){output_width=w;output_height=h;}
void glutInitWindowPosition(int x,int y){assert(x>=-32768&&y>=-32768);initial_x=x;initial_y=y;}
int glutCreateWindow(const char *title)
{
    struct Test_window *w;
    int probe;
    unsigned int i;
    assert(title&&available(requested_mode)&&next_window<64);
    probe=strcmp(title,"testglobe pixel-format probe")==0;
    if(probe) {
#ifdef TEST_OGL_FREEGLUT
        assert(!"FreeGLUT maps probe windows; none may be created");
#endif
        assert(final_windows==0&&output_width==1&&output_height==1);
#ifndef OGL_GLUT_STRINGS
        /* Without display strings, repeated mode bits find the same visual. */
        for(i=0;i<probed_count;++i)assert(probed_modes[i]!=requested_mode);
        probed_modes[probed_count++]=requested_mode;
#endif
        ++probe_windows;
    }
    else {
        assert(strcmp(title,TESTGLOBE_NAME)==0);
#if defined(TEST_OGL_FREEGLUT)
        /* Only a shallow RGB window may be replaced, by color index. */
        assert(probe_windows==0&&(final_windows==0||(final_windows==1&&strcmp(profile,"rgb8")==0)));
        assert((final_windows==0)==((requested_mode&GLUT_INDEX)==0||software_profile()));
#elif defined(GLUT_DISPLAY_MODE_POSSIBLE)
        assert(final_windows==0&&probe_windows>0&&probe_destroys==probe_windows);
#else
        assert(final_windows==0&&probe_windows==0&&probe_destroys==0);
        assert(requested_mode==(GLUT_RGB|GLUT_DOUBLE));
        assert(display_mode_calls==1&&display_string_calls==0&&mode_queries==0);
#endif
        assert(window_count==destroy_count&&active_window==0);
        assert(output_width==64&&output_height==48);
        assert(initial_x==(int)drv_win_x&&initial_y==(int)drv_win_y);
        ++final_windows;
    }
    (void)i;
    active_window=next_window++;w=&windows[active_window];memset(w,0,sizeof(*w));
    w->probe=probe;
    w->live=1;w->indexed=(requested_mode&GLUT_INDEX)!=0;w->doubled=(requested_mode&GLUT_DOUBLE)!=0;
    w->depth=(requested_mode&GLUT_DEPTH)?24:0;w->width=output_width;w->height=output_height;
    if(!w->indexed){w->red=w->green=w->blue=8;w->alpha=(requested_mode&GLUT_ALPHA)?8:0;
        if(strcmp(profile,"unknownalpha")==0&&(requested_mode&GLUT_ALPHA))w->alpha=-1;
        if(strcmp(profile,"rgb16")==0){w->red=5;w->green=6;w->blue=5;w->alpha=0;}
        if(shallow_rgb_profile()){w->red=3;w->green=3;w->blue=2;w->alpha=0;}}
    ++window_count;return active_window;
}
void glutDestroyWindow(int window){assert(window>0&&windows[window].live&&windows[window].display!=NULL);if(windows[window].probe)++probe_destroys;windows[window].live=0;++destroy_count;if(window==active_window)active_window=0;}
int glutGetWindow(void){return active_window;}
void glutSetWindow(int window){assert(window>0&&windows[window].live);active_window=window;}
void glutHideWindow(void){assert(active_window&&windows[active_window].live);initial_redraw();}
void glutShowWindow(void){assert(active_window&&windows[active_window].live&&!windows[active_window].probe);initial_redraw();}
void glutSetWindowTitle(const char *s){assert(s&&strcmp(s,TESTGLOBE_NAME)==0);}
void glutPositionWindow(int x,int y){assert(x>=0&&y>=0);}
void glutReshapeWindow(int w,int h){assert(w>0&&h>0);initial_redraw();windows[active_window].width=w;windows[active_window].height=h;}
void glutDisplayFunc(void (*f)(void)){struct Test_window *w=&windows[active_window];assert(active_window&&w->live&&f!=NULL);if(f==drv_render)assert(globe_verts!=NULL&&!w->probe);if(w->probe&&w->display==NULL)++probe_callbacks;w->display=f;}
void glutReshapeFunc(void (*f)(int,int)){reshape_callback=f;}
void glutMouseFunc(void (*f)(int,int,int,int)){mouse_callback=f;}
void glutMotionFunc(void (*f)(int,int)){motion_callback=f;}
void glutPassiveMotionFunc(void (*f)(int,int)){assert(f==motion_callback);}
void glutKeyboardFunc(void (*f)(unsigned char,int,int)){keyboard_callback=f;}
void glutPostRedisplay(void){++redisplay_posts;}
void glutSwapBuffers(void){assert(windows[active_window].doubled);++swaps;}
void glutMainLoop(void){assert(globe_verts!=NULL&&windows[active_window].display==drv_render&&reshape_callback&&mouse_callback&&motion_callback&&keyboard_callback);assert(redisplay_posts>posts_before_loop);++loop_calls;}
int glutBitmapWidth(void *font,int c)
{
    assert(c>0&&c<256);
    ++bitmap_width_calls;
    if(font==GLUT_BITMAP_HELVETICA_10)return c==' '?3:5;
    if(font==GLUT_BITMAP_HELVETICA_12)return c==' '?4:6;
    assert(font==GLUT_BITMAP_HELVETICA_18);return c==' '?5:9;
}
/* One glyph reaches the framebuffer, directly or from its glyph list. */
static void record_glyph(void *font,int c)
{
    assert(font&&c>0&&c<256&&osd_line>=0&&osd_line<2);
    osd_row_width+=glutBitmapWidth(font,c);--bitmap_width_calls;
    assert(osd_row_width<=(int)drv_win_width-20);
    assert(osd_lengths[osd_line]<sizeof(osd_lines[osd_line])-1U);
    osd_lines[osd_line][osd_lengths[osd_line]++]=(char)c;
    osd_lines[osd_line][osd_lengths[osd_line]]=0;
}
void glutBitmapCharacter(void *font,int c)
{
    struct Glyph_block *block;
    assert(font&&c>0&&c<256);
    if(glyph_compiling>=0){
        /* GLUT's pixel-store reads and changes run now; the bitmap is kept. */
        block=&glyph_blocks[glyph_compiling];
        assert(c==glyph_char&&(block->font==NULL||block->font==font));
        block->font=font;block->chars[c]=c;return;
    }
    ++direct_glyphs;record_glyph(font,c);
}
void glutSetColor(int i,GLfloat r,GLfloat g,GLfloat b)
{assert(windows[active_window].indexed&&i>=0&&i<256);assert(fabs(r-(double)earth_cmap[i][0]/255)<1e-6&&fabs(g-(double)earth_cmap[i][1]/255)<1e-6&&fabs(b-(double)earth_cmap[i][2]/255)<1e-6);++palette_calls;palette_mask[i]=1;}
GLenum glGetError(void){GLenum code=pending_error;++get_error_calls;pending_error=following_error;following_error=GL_NO_ERROR;return code;}
void glGetIntegerv(GLenum query,GLint *value)
{
    struct Test_window *w=&windows[active_window];assert(w->live);
    switch(query){
    case GL_RED_BITS:*value=w->red;break;case GL_GREEN_BITS:*value=w->green;break;
    case GL_BLUE_BITS:*value=w->blue;break;case GL_ALPHA_BITS:*value=w->alpha;break;
    case GL_INDEX_BITS:*value=w->indexed?8:0;break;case GL_DEPTH_BITS:*value=w->depth;break;
    case GL_RGBA_MODE:*value=!w->indexed;break;case GL_DOUBLEBUFFER:*value=w->doubled;break;
    case GL_MATRIX_MODE:++matrix_queries;*value=(GLint)matrix_mode;break;
    case GL_TEXTURE_BINDING_2D:
        ++binding_queries;*value=(GLint)texture_binding;
        if(texture_profile("bindinglost")){texture_injection=0;*value=0;}
        break;
    case GL_MAX_TEXTURE_SIZE:++capacity_queries;*value=texture_profile("maxzero")?0:texture_profile("maxtiny")?16:(GLint)maximum_texture_size;break;
    case GL_UNPACK_ALIGNMENT:*value=unpack_alignment;break;
    case GL_INDEX_SHIFT:*value=index_shift;break;case GL_INDEX_OFFSET:*value=index_offset;break;case GL_MAP_COLOR:*value=map_color;break;
    default:assert(!"Unmodeled GL integer query");*value=0;break;
    }
}
void glGetBooleanv(GLenum query,GLboolean *value){GLint i;glGetIntegerv(query,&i);*value=(GLboolean)(i!=0);}
GLboolean glIsEnabled(GLenum capability){++enabled_queries;if(capability==GL_TEXTURE_2D)return (GLboolean)texture_enabled;if(capability==GL_DEPTH_TEST)return (GLboolean)depth_enabled;if(capability==GL_CULL_FACE)return (GLboolean)cull_enabled;assert(!"Unexpected enabled query");return 0;}
void glPixelTransferi(GLenum parameter,GLint value){if(parameter==GL_INDEX_SHIFT)index_shift=value;else if(parameter==GL_INDEX_OFFSET)index_offset=value;else if(parameter==GL_MAP_COLOR)map_color=value;else assert(!"Unexpected pixel transfer");}
void glPushAttrib(GLbitfield mask)
{struct Test_attrib *a;assert(attrib_depth<16);a=&attribs[attrib_depth++];a->mask=mask;a->texture=texture_enabled;a->depth=depth_enabled;a->cull=cull_enabled;a->shift=index_shift;a->offset=index_offset;a->map=map_color;a->x=zoom_x;a->y=zoom_y;a->index=last_index;memcpy(a->rgb,last_rgb,3);a->base=list_base;}
void glPopAttrib(void)
{struct Test_attrib *a;assert(attrib_depth);a=&attribs[--attrib_depth];if(a->mask&GL_ENABLE_BIT){texture_enabled=a->texture;depth_enabled=a->depth;cull_enabled=a->cull;}if(a->mask&GL_PIXEL_MODE_BIT){index_shift=a->shift;index_offset=a->offset;map_color=a->map;zoom_x=a->x;zoom_y=a->y;}if(a->mask&GL_CURRENT_BIT){last_index=a->index;memcpy(last_rgb,a->rgb,3);}if(a->mask&GL_LIST_BIT)list_base=a->base;}
void glPushClientAttrib(GLbitfield mask){assert(mask==GL_CLIENT_PIXEL_STORE_BIT&&client_depth<16);client_alignment[client_depth++]=unpack_alignment;}
void glPopClientAttrib(void){assert(client_depth);unpack_alignment=client_alignment[--client_depth];}
void glTexEnvi(GLenum target,GLenum parameter,GLint value){assert(target==GL_TEXTURE_ENV&&parameter==GL_TEXTURE_ENV_MODE&&value==GL_REPLACE);}
void glGetFloatv(GLenum query,GLfloat *value)
{unsigned int s;++float_queries;if(query==GL_ZOOM_X){*value=zoom_x;return;}if(query==GL_ZOOM_Y){*value=zoom_y;return;}assert(query==GL_MODELVIEW_MATRIX||query==GL_PROJECTION_MATRIX);s=query==GL_PROJECTION_MATRIX?1U:0U;memcpy(value,matrices[s][stack_depth[s]],16*sizeof(*value));}
static void reset_frame(void)
{osd_lengths[0]=osd_lengths[1]=0;osd_lines[0][0]=osd_lines[1][0]=0;memset(&frame_geometry,0,sizeof(frame_geometry));frame_list_successes=frame_texture_pixels=0;}
void glClear(GLbitfield mask){reset_frame();assert((mask&GL_COLOR_BUFFER_BIT)!=0);assert(((mask&GL_DEPTH_BUFFER_BIT)!=0)==(windows[active_window].depth>0));++frame_clears;}
void glClearColor(GLclampf r,GLclampf g,GLclampf b,GLclampf a){assert(r==0&&g==0&&b==0&&a==1);}
void glClearIndex(GLfloat index){assert(index>=0&&index<256);last_clear_index=(int)index;}
void glEnable(GLenum c)
{
    if(c==GL_TEXTURE_2D){
        /* The enable request does not take effect once after an upload. */
        if(texture_profile("disabledtexture")){texture_injection=0;return;}
        texture_enabled=1;
    }
    else if(c==GL_DEPTH_TEST){assert(windows[active_window].depth>0);depth_enabled=1;}else if(c==GL_CULL_FACE)cull_enabled=1;else assert(!"Unexpected glEnable");
}
void glDisable(GLenum c){if(c==GL_TEXTURE_2D)texture_enabled=0;else if(c==GL_DEPTH_TEST)depth_enabled=0;else if(c==GL_CULL_FACE)cull_enabled=0;else assert(!"Unexpected glDisable");}
void glFrontFace(GLenum mode){assert(mode==GL_CCW);}
void glCullFace(GLenum mode){assert(mode==GL_BACK);}
void glMatrixMode(GLenum mode){assert(mode==GL_MODELVIEW||mode==GL_PROJECTION);matrix_mode=mode;}
void glLoadIdentity(void){matrix_identity(current_matrix());}
void glPushMatrix(void){unsigned int s=matrix_slot();assert(stack_depth[s]<15);memcpy(matrices[s][stack_depth[s]+1],current_matrix(),16*sizeof(GLfloat));++stack_depth[s];}
void glPopMatrix(void){unsigned int s=matrix_slot();assert(stack_depth[s]);--stack_depth[s];}
void glTranslatef(GLfloat x,GLfloat y,GLfloat z){GLfloat m[16];matrix_identity(m);m[12]=x;m[13]=y;m[14]=z;postmultiply(m);}
void glRotatef(GLfloat angle,GLfloat x,GLfloat y,GLfloat z)
{
    GLfloat m[16];double c=cos(angle*3.14159265358979323846/180.0),s=sin(angle*3.14159265358979323846/180.0),t=1-c;
    matrix_identity(m);m[0]=(GLfloat)(t*x*x+c);m[4]=(GLfloat)(t*x*y-s*z);m[8]=(GLfloat)(t*x*z+s*y);
    m[1]=(GLfloat)(t*x*y+s*z);m[5]=(GLfloat)(t*y*y+c);m[9]=(GLfloat)(t*y*z-s*x);
    m[2]=(GLfloat)(t*x*z-s*y);m[6]=(GLfloat)(t*y*z+s*x);m[10]=(GLfloat)(t*z*z+c);postmultiply(m);
}
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){assert(x==0&&y==0&&w>0&&h>0);}
GLuint glGenLists(GLsizei n)
{
    unsigned int k;
    if(n==256){
        assert(!current_list&&glyph_compiling<0&&!begin_mode);
        ++glyph_block_attempts;
        if(strcmp(profile,"listfail")==0||strcmp(profile,"fontlistfail")==0)return 0;
        for(k=0;k<GLYPH_BLOCKS&&glyph_blocks[k].live;++k){}
        assert(k<GLYPH_BLOCKS);
        memset(&glyph_blocks[k],0,sizeof(glyph_blocks[k]));glyph_blocks[k].live=1;++glyph_block_builds;
        return GLYPH_BASE+256U*k;
    }
    assert(n==1&&native_lists<63);++list_attempts;if(strcmp(profile,"listfail")==0)return 0;++native_lists;list_reserved[native_lists]=1;return native_lists;
}
void glNewList(GLuint l,GLenum mode)
{
    if(l>=GLYPH_BASE){
        unsigned int k=(l-GLYPH_BASE)/256U;
        assert(k<GLYPH_BLOCKS&&glyph_blocks[k].live&&(l-GLYPH_BASE)%256U!=0U);
        assert(mode==GL_COMPILE&&!current_list&&glyph_compiling<0&&!begin_mode);
        glyph_compiling=(int)k;glyph_char=(int)((l-GLYPH_BASE)%256U);return;
    }
    assert(l&&l<64&&list_reserved[l]&&mode==GL_COMPILE&&!current_list&&!begin_mode&&glyph_compiling<0);if(strcmp(profile,"newlistfail")==0){inject(GL_OUT_OF_MEMORY);return;}current_list=l;list_exists[l]=1;memset(&list_geometry[l],0,sizeof(list_geometry[l]));
}
void glEndList(void)
{
    if(glyph_compiling>=0){
        /* One glyph exhausts list memory once; the driver must notice. */
        if(texture_injection&&strcmp(profile,"fontcompilefail")==0&&glyph_char=='A'){texture_injection=0;inject(GL_OUT_OF_MEMORY);}
        glyph_compiling=-1;return;
    }
    assert(current_list&&!begin_mode);current_list=0;
}
GLboolean glIsList(GLuint l){assert(l&&l<64&&list_reserved[l]&&!current_list&&!begin_mode);++list_validations;return strcmp(profile,"validationfail")!=0&&list_exists[l]?GL_TRUE:GL_FALSE;}
void glCallList(GLuint l){assert(l&&l<64&&list_exists[l]&&!list_compile_failed[l]&&!current_list&&!begin_mode);++list_calls;if(strcmp(profile,"callfail")==0||fail_next_call){fail_next_call=0;inject(GL_INVALID_OPERATION);return;}++frame_list_successes;geometry_add(&frame_geometry,&list_geometry[l]);}
void glListBase(GLuint base){assert(base>=GLYPH_BASE);list_base=base;}
/* Each byte names a list relative to the base. Every name must fall in a
 * live glyph block built for the font the driver currently measures with. */
void glCallLists(GLsizei n,GLenum type,const GLvoid *lists)
{
    const GLubyte *bytes=(const GLubyte *)lists;
    struct Glyph_block *block;
    GLuint name;
    GLsizei i;
    assert(n>0&&type==GL_UNSIGNED_BYTE&&bytes&&!current_list&&glyph_compiling<0&&!begin_mode);
    for(i=0;i<n;++i){
        name=list_base+bytes[i];
        assert(name>=GLYPH_BASE&&(name-GLYPH_BASE)/256U<GLYPH_BLOCKS);
        block=&glyph_blocks[(name-GLYPH_BASE)/256U];
        assert(block->live&&block->font==ogl_glut_font&&(name-GLYPH_BASE)%256U==bytes[i]);
        if(block->chars[bytes[i]])record_glyph(block->font,block->chars[bytes[i]]);
    }
    ++call_lists_calls;
}
void glDeleteLists(GLuint l,GLsizei n)
{
    if(n==256){
        unsigned int k=(l-GLYPH_BASE)/256U;
        assert(l>=GLYPH_BASE&&(l-GLYPH_BASE)%256U==0U&&k<GLYPH_BLOCKS&&glyph_blocks[k].live&&glyph_compiling<0);
        glyph_blocks[k].live=0;++glyph_block_deletions;return;
    }
    assert(l&&l<64&&list_reserved[l]&&n==1&&!current_list&&!begin_mode);list_exists[l]=list_reserved[l]=0;++list_deletions;
}
void glBegin(GLenum mode){assert((mode==GL_TRIANGLES||mode==GL_TRIANGLE_STRIP)&&!begin_mode);begin_mode=mode;block_vertices=block_coordinates=block_colors=0;}
void glEnd(void)
{
    struct Test_geometry geometry;
    assert(begin_mode);
    if(globe_verts!=NULL){
        memset(&geometry,0,sizeof(geometry));
        geometry.vertices=block_vertices;
        if(begin_mode==GL_TRIANGLE_STRIP){
            assert(block_vertices>=3U&&block_vertices<=2U*((unsigned int)globe_longitude+1U));
            if(!drv_cull_enabled)assert(block_vertices==2U*((unsigned int)globe_longitude+1U));
            geometry.triangles=block_vertices-2U;geometry.strip_batches=1;
            if(texture_enabled&&!drv_cull_enabled){
                assert(first_uvs[0][0]==0&&first_uvs[1][0]==0);
                assert(block_uvs[0][0]==1&&block_uvs[1][0]==1);
            }
        }else{
            if(drv_cull_enabled)assert(block_vertices%3U==0U);
            else{
#ifdef TRI
            assert(block_vertices==3UL*globe_tris_num);
#else
            assert(block_vertices==6U*(unsigned int)globe_longitude);
#endif
            }
            geometry.triangles=block_vertices/3U;geometry.cap_batches=1;
        }
        assert(block_coordinates==(texture_enabled?block_vertices:0U));
        if(texture_enabled)geometry.textured_vertices=block_vertices;
        if(current_list){if(!list_compile_failed[current_list])geometry_add(&list_geometry[current_list],&geometry);}
        else geometry_add(&frame_geometry,&geometry);
        /* Native texturing fails once while drawing, outside any list. */
        if(texture_enabled&&!current_list&&texture_profile("texdrawfail")){texture_injection=0;inject(GL_INVALID_OPERATION);}
    }
    begin_mode=0;
    if(fail_traversal)drv_abort_frame("injected traversal failure");
}
/* Flat colors persist within a strip or batch; a repeat would be redundant. */
void glColor3ub(GLubyte r,GLubyte g,GLubyte b)
{
    if(begin_mode&&globe_verts!=NULL){
        if(block_colors&&last_rgb[0]==r&&last_rgb[1]==g&&last_rgb[2]==b&&!rgb_shared(r,g,b))++redundant_colors;
        ++block_colors;
    }
    last_rgb[0]=r;last_rgb[1]=g;last_rgb[2]=b;++rgb_calls;
}
void glColor3f(GLfloat r,GLfloat g,GLfloat b){glColor3ub((GLubyte)(r*255),(GLubyte)(g*255),(GLubyte)(b*255));}
void glIndexub(GLubyte i)
{
    if(begin_mode&&globe_verts!=NULL){if(block_colors&&last_index==i)++redundant_colors;++block_colors;}
    last_index=i;++indices;
}
void glIndexi(GLint i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glIndexf(GLfloat i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glVertex3fv(const GLfloat *v)
{
    unsigned int slot;
    double xy,t,u,difference;
    assert(begin_mode&&v);
    if(current_list&&strcmp(profile,"listcompilefail")==0&&!list_compile_failed[current_list]&&block_vertices==8U){
        inject(GL_OUT_OF_MEMORY);list_compile_failed[current_list]=1;
    }
    if(globe_verts!=NULL){
        /* Each strip or batch sets its own first color, including in lists. */
        if(!texture_enabled&&block_vertices==0U)assert(block_colors>0U);
        slot=begin_mode==GL_TRIANGLES?block_vertices%3U:(block_vertices<2U?block_vertices:2U);
        memcpy(block_polygon[slot],v,sizeof(block_polygon[slot]));
        if(texture_enabled){
            assert(block_coordinates==block_vertices+1U);
            memcpy(block_uvs[slot],last_uv,sizeof(last_uv));
            if(block_vertices<2U)memcpy(first_uvs[block_vertices],last_uv,sizeof(last_uv));
            xy=sqrt((double)v[0]*v[0]+(double)v[1]*v[1]);
            t=atan2(xy,(double)v[2])/TEST_PI;assert(fabs(t-last_uv[1])<1e-5);
            if(xy>0.001){
                u=atan2((double)v[1],(double)v[0])/(2.0*TEST_PI);if(u<0)u+=1;
                difference=fabs(u-last_uv[0]);if(difference>0.5)difference=1-difference;
                assert(difference<1e-5);
            }
        }else assert(block_coordinates==0);
        if(begin_mode==GL_TRIANGLES){
            if(slot==2U)check_triangle(block_polygon[0],block_polygon[1],block_polygon[2]);
        }else if(block_vertices>=2U){
            if((block_vertices&1U)!=0)check_triangle(block_polygon[1],block_polygon[0],block_polygon[2]);
            else check_triangle(block_polygon[0],block_polygon[1],block_polygon[2]);
            memcpy(block_polygon[0],block_polygon[1],sizeof(block_polygon[0]));
            memcpy(block_polygon[1],block_polygon[2],sizeof(block_polygon[1]));
            memcpy(block_uvs[0],block_uvs[1],sizeof(block_uvs[0]));
            memcpy(block_uvs[1],block_uvs[2],sizeof(block_uvs[1]));
        }
    }
    ++block_vertices;++emitted_vertices;
}
void glTexCoord2fv(const GLfloat *v){assert(begin_mode&&v&&v[0]>=0&&v[0]<=1&&v[1]>=0&&v[1]<=1);memcpy(last_uv,v,sizeof(last_uv));++block_coordinates;++texture_coordinates;}
void glRasterPos2i(GLint x,GLint y)
{
    assert(x>=0&&x<=(int)drv_win_width&&y>=0&&y<=(int)drv_win_height);
    osd_line=-1;
    if(x==10){
        assert(y-ogl_glut_font_descent>=4&&y+ogl_glut_font_ascent<=(int)drv_win_height-4);
        osd_line=y>(int)drv_win_height/2?1:0;
        if(osd_lengths[osd_line]!=0){assert(osd_lengths[osd_line]<255U);osd_lines[osd_line][osd_lengths[osd_line]++]=' ';osd_lines[osd_line][osd_lengths[osd_line]]=0;}
        osd_row_width=0;++osd_rows;++osd_half_rows[osd_line];
    }
}
void glRasterPos2f(GLfloat x,GLfloat y){assert(x>=-1&&x<=drv_win_width&&y>=-1&&y<=drv_win_height);}
void glPixelStorei(GLenum p,GLint v){if(p==GL_UNPACK_ALIGNMENT){assert(v==1||v==2||v==4||v==8);unpack_alignment=v;}else assert(v==0);}
void glPixelZoom(GLfloat x,GLfloat y){zoom_x=x;zoom_y=y;}
void glDrawPixels(GLsizei w,GLsizei h,GLenum format,GLenum type,const GLvoid *data)
{
    unsigned long i,count;const GLubyte *bytes=data;unsigned char *source=texsoft_index_pixels();int nonbackground=0;
    assert(w==(int)drv_win_width&&h==(int)drv_win_height&&format==GL_COLOR_INDEX&&type==GL_UNSIGNED_BYTE&&unpack_alignment==1);
    assert(index_shift==0&&index_offset==0&&!map_color&&zoom_x==1&&zoom_y==1);
    assert(bytes&&source&&data==source&&texsoft_pixels()==NULL);count=(unsigned long)w*h;
    for(i=0;i<count;++i){assert(bytes[i]==source[i]);if(bytes[i]!=last_clear_index)nonbackground=1;}
    assert(nonbackground);++draw_pixels_calls;
    /* The raster replaces every window pixel, as a clear would. */
    reset_frame();
    if(texture_profile("softtransfererror")){inject(GL_INVALID_OPERATION);return;}
    ++frame_texture_pixels;
}
void glGenTextures(GLsizei n,GLuint *textures){assert(n==1);++texture_allocations;if(texture_profile("texallocerror")){*textures=0;inject(GL_OUT_OF_MEMORY);}else {*textures=1;texture_min_filter=texture_mag_filter=0;}}
void glDeleteTextures(GLsizei n,const GLuint *textures){assert(n==1&&*textures==1);texture_exists=0;if(texture_binding==*textures)texture_binding=0;++texture_deletions;}
void glBindTexture(GLenum target,GLuint texture){assert(target==GL_TEXTURE_2D&&texture<=1);texture_binding=texture;if(texture)texture_exists=1;}
GLboolean glIsTexture(GLuint texture)
{
    assert(texture<=1);++is_texture_calls;
    /* The implementation reports the new object invalid once. */
    if(texture&&texture_profile("losttexture")){texture_injection=0;return GL_FALSE;}
    return texture&&texture_exists?GL_TRUE:GL_FALSE;
}
void glTexParameteri(GLenum target,GLenum p,GLint value)
{assert(target==GL_TEXTURE_2D&&value==GL_LINEAR);if(p==GL_TEXTURE_MIN_FILTER)texture_min_filter=value;else {assert(p==GL_TEXTURE_MAG_FILTER);texture_mag_filter=value;}}
static void texture_upload(GLsizei w,GLsizei h,const GLvoid *data)
{
    const GLubyte *rgb=data;unsigned long i;
    assert(w>=32&&w<=512&&h==w/2&&rgb);
    for(i=0;i<(unsigned long)w*h;++i){unsigned int v=test_earth_data(w)[i];assert(rgb[i*3]==earth_cmap[v][0]&&rgb[i*3+1]==earth_cmap[v][1]&&rgb[i*3+2]==earth_cmap[v][2]);}
    ++texture_uploads;
}
void glTexImage2D(GLenum t,GLint l,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum f,GLenum type,const GLvoid *p)
{
    assert(t==GL_TEXTURE_2D&&l==0&&internal==GL_RGB&&border==0&&f==GL_RGB&&type==GL_UNSIGNED_BYTE);
    assert(texture_min_filter==GL_LINEAR&&texture_mag_filter==GL_LINEAR);
    assert(w<=(GLsizei)maximum_texture_size&&h<=(GLsizei)maximum_texture_size);
    ++direct_texture_uploads;
    if(texture_profile("uploadfail")||(strcmp(profile,"uploadreduced")==0&&w>128)){inject(GL_OUT_OF_MEMORY);return;}
    if(texture_profile("uploadinvalid")){inject(GL_INVALID_OPERATION);return;}
    if(texture_profile("uploadmixed")){inject(GL_OUT_OF_MEMORY);inject(GL_INVALID_OPERATION);return;}
    if(texture_profile("uploadmixedreverse")){inject(GL_INVALID_OPERATION);inject(GL_OUT_OF_MEMORY);return;}
    texture_upload(w,h,p);
}
void glFlush(void){++flushes;}
void glShadeModel(GLenum mode){assert(mode==GL_FLAT);}
void glDepthFunc(GLenum function){assert(function==GL_LEQUAL);}
void gluPerspective(GLdouble fov,GLdouble aspect,GLdouble near_plane,GLdouble far_plane)
{GLfloat m[16];double f=1/tan(fov*3.14159265358979323846/360.0);memset(m,0,sizeof(m));m[0]=(GLfloat)(f/aspect);m[5]=(GLfloat)f;m[10]=(GLfloat)((far_plane+near_plane)/(near_plane-far_plane));m[11]=-1;m[14]=(GLfloat)(2*far_plane*near_plane/(near_plane-far_plane));postmultiply(m);}
void gluOrtho2D(GLdouble l,GLdouble r,GLdouble b,GLdouble t)
{GLfloat m[16];matrix_identity(m);m[0]=(GLfloat)(2/(r-l));m[5]=(GLfloat)(2/(t-b));m[10]=-1;m[12]=(GLfloat)(-(r+l)/(r-l));m[13]=(GLfloat)(-(t+b)/(t-b));postmultiply(m);}
static void draw_osd_checked(void)
{
    GLint previous;
    glGetIntegerv(GL_MATRIX_MODE,&previous);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();
    glMatrixMode(GL_PROJECTION);glPushMatrix();
    drv_draw_osd();
    glMatrixMode(GL_PROJECTION);glPopMatrix();
    glMatrixMode(GL_MODELVIEW);glPopMatrix();
    glMatrixMode((GLenum)previous);
}

/* A resize selects the font and, with a current context, its glyph lists. */
static void select_font_checked(int width,int height)
{
    drv_win_width=(unsigned int)width;drv_win_height=(unsigned int)height;
    ogl_glut_select_font(width,height);ogl_prepare_font();
}

/* Geometry/raster tests use 64x48 for speed. Check full status semantics in
 * a separate fitting viewport; tiny-window layout is checked independently. */
static void capture_osd_full(void)
{
    unsigned int width=drv_win_width,height=drv_win_height;
    select_font_checked(1280,768);
    osd_lengths[0]=osd_lengths[1]=0;osd_lines[0][0]=osd_lines[1][0]=0;
    draw_osd_checked();
    select_font_checked((int)width,(int)height);
}

static unsigned int help_rows(int width,int height)
{
    select_font_checked(width,height);osd_rows=0;osd_half_rows[0]=osd_half_rows[1]=0;
    osd_lengths[0]=osd_lengths[1]=0;osd_lines[0][0]=osd_lines[1][0]=0;
    draw_osd_checked();
    assert(strcmp(osd_lines[0],drv_help)==0);
    return osd_half_rows[0];
}

static void check_font_layout(void)
{
    unsigned int width=drv_win_width,height=drv_win_height,narrow,wide,widths;
    select_font_checked(320,240);osd_rows=0;osd_lengths[0]=osd_lengths[1]=0;
    draw_osd_checked();assert(ogl_glut_font==GLUT_BITMAP_HELVETICA_10&&osd_rows>=4);
    assert(strcmp(osd_lines[0],drv_help)==0);
    select_font_checked(640,480);osd_rows=0;osd_lengths[0]=osd_lengths[1]=0;
    draw_osd_checked();assert(ogl_glut_font==GLUT_BITMAP_HELVETICA_12&&osd_rows>=2);
    assert(strcmp(osd_lines[0],drv_help)==0);
    select_font_checked(1280,768);osd_rows=0;osd_lengths[0]=osd_lengths[1]=0;
    draw_osd_checked();assert(ogl_glut_font==GLUT_BITMAP_HELVETICA_18&&osd_rows==2);
    assert(strcmp(osd_lines[0],drv_help)==0);
    drv_win_width=16;drv_win_height=32;select_font_checked(16,32);osd_rows=0;
    draw_osd_checked();assert(osd_rows==0);
    /* The cached help wrapping follows width changes within one font, and
     * returns to the narrower layout; drawing it needs no width queries. */
    narrow=help_rows(240,240);wide=help_rows(400,240);
    assert(ogl_glut_font==GLUT_BITMAP_HELVETICA_10&&narrow>wide&&wide>=1U);
    widths=bitmap_width_calls;
    assert(help_rows(240,240)==narrow);
    assert(bitmap_width_calls==widths);
    select_font_checked((int)width,(int)height);
    puts("PASS OpenGL OSD fonts: classic GLUT sizes follow viewport, proportional widths wrap, glyphs stay inside bounds and tiny windows suppress text");
}

static void check_dlist_osd(void)
{
    const char *option,*state,*fps,*texture,*expected_state;
    char expected_texture[64],dimensions[24];
    capture_osd_full();
    option=strstr(osd_lines[0],"T = texture mapping, D = display list");
    assert(option&&strcmp(option,"T = texture mapping, D = display list")==0);
    assert(strcmp(osd_lines[0],drv_help)==0);
    assert(strstr(osd_lines[0],"C = cull")!=NULL);
    assert(strstr(osd_lines[1],drv_cull_enabled?", cull on":", cull off")!=NULL);
    expected_state=(!ogl_dlist_enabled||drv_cull_enabled)?", dlist off":
                    (frame_list_successes?", dlist on":", dlist fail");
    state=strstr(osd_lines[1],expected_state);
    assert(state);
    texture=state+strlen(expected_state);
    strcpy(expected_texture,!drv_texture_enabled?", texture off":
           (frame_texture_pixels?", texture soft":
            (frame_geometry.textured_vertices?", texture on":", texture fail")));
    if(ogl_texture_used&&texture_size!=TEXTURE_DEFAULT_SIZE){
        sprintf(dimensions," (%u)",texture_size);strcat(expected_texture,dimensions);
    }
    assert(strcmp(texture,expected_texture)==0);
    assert(strstr(osd_lines[1],"texture mapping")==NULL);
    fps=strstr(osd_lines[1],"frames per second");
    if(drv_fps_enabled){
        assert(fps&&fps<state);
        assert(strstr(osd_lines[1],", cull ")<state);
    }else assert(fps==NULL);
}

static void render_checked(int indexed)
{
    unsigned int previous_pixels=draw_pixels_calls;
    unsigned int previous_osd=osd_rows;
    unsigned int previous_attempts=list_attempts,previous_calls=list_calls;
    unsigned int previous_validations=list_validations;
    unsigned int errors=get_error_calls,raised=error_events,clears=frame_clears;
    unsigned int textures=is_texture_calls,bindings=binding_queries,enables=enabled_queries;
    unsigned int modes=matrix_queries,floats=float_queries,widths=bitmap_width_calls;
    unsigned int direct=direct_glyphs,redundant=redundant_colors;
    drv_render();
    /* Synchronous queries: one error check per steady frame, texture state
     * only when it changes, list validation only after compiling, and no
     * GLUT glyph or width calls once glyph lists exist. */
    assert(is_texture_calls==textures&&binding_queries==bindings&&enabled_queries==enables&&matrix_queries==modes);
    assert(bitmap_width_calls==widths&&redundant_colors==redundant);
    if(list_attempts==previous_attempts)assert(list_validations==previous_validations);
    if(ogl_glut_glyph_state[ogl_glut_tier]>0)assert(direct_glyphs==direct);
    assert(float_queries==floats||(indexed&&float_queries==floats+2U));
    if(error_events==raised&&!drv_frame_failed){
        if(list_attempts==previous_attempts)assert(get_error_calls==errors+1U);
        /* The software raster replaces every pixel; others clear once. */
        assert(frame_clears==clears+(indexed&&ogl_texture_used?0U:1U));
    }
    assert(ogl_dlist_used==(frame_list_successes!=0));
    assert(ogl_texture_used==((frame_geometry.textured_vertices||frame_texture_pixels)!=0));
    if(ogl_texture_used&&!indexed)assert(texture_min_filter==GL_LINEAR&&texture_mag_filter==GL_LINEAR);
    if(!ogl_dlist_enabled)
        assert(list_attempts==previous_attempts&&list_calls==previous_calls&&dlist==0);
    if(drv_cull_enabled)
        assert(!ogl_dlist_used&&list_attempts==previous_attempts&&
               list_calls==previous_calls&&list_validations==previous_validations);
    if(indexed&&drv_texture_enabled&&!ogl_texture_failed){
        assert(!ogl_dlist_used&&list_attempts==previous_attempts&&
               list_calls==previous_calls&&list_validations==previous_validations);
        assert(draw_pixels_calls==previous_pixels+1U);
        assert(frame_geometry.vertices==0&&frame_geometry.triangles==0);
    }else{
        assert(draw_pixels_calls==previous_pixels||
               (ogl_texture_failed&&draw_pixels_calls==previous_pixels+1U));
        if(drv_cull_enabled){
            assert(frame_geometry.triangles>0&&frame_geometry.triangles<globe_tris_num);
        }else{
        assert(frame_geometry.triangles==globe_tris_num);
        assert(frame_geometry.cap_batches==1);
#ifdef TRI
        assert(frame_geometry.vertices==3UL*globe_tris_num);
        assert(frame_geometry.strip_batches==0);
#else
        assert(frame_geometry.vertices==6UL*globe_longitude+
               2UL*(globe_longitude+1UL)*(globe_latitude-2UL));
        assert(frame_geometry.strip_batches==(unsigned long)globe_latitude-2UL);
#endif
        }
    }
    if(!ogl_dlist_enabled)assert(list_validations==previous_validations);
    if(ogl_dlist_enabled&&!(indexed&&drv_texture_enabled)&&
       (strcmp(profile,"listfail")==0||strcmp(profile,"newlistfail")==0||
        strcmp(profile,"listcompilefail")==0||strcmp(profile,"validationfail")==0||
        strcmp(profile,"callfail")==0))assert(!ogl_dlist_used&&dlist==0);
    assert(!begin_mode&&!current_list&&glyph_compiling<0);
    if(drv_texture_enabled&&ogl_texture_failed)assert(frame_geometry.textured_vertices==0&&!frame_texture_pixels);
    if(drv_osd_enabled){assert(osd_rows>previous_osd);check_dlist_osd();}
}

static void check_failed_frame(void)
{
    unsigned int posts, i;
    float old_x, old_y;
    keyboard_callback('d',0,0);
    assert(!ogl_dlist_enabled);
    drv_auto_enabled=TRUE;drv_osd_enabled=FALSE;fail_traversal=TRUE;
    old_x=drv_rot_x;old_y=drv_rot_y;
    for(i=0;i<2U;++i){
        osd_lengths[0]=osd_lengths[1]=0;
        osd_lines[0][0]=osd_lines[1][0]=0;
        posts=redisplay_posts;osd_rows=0;
        drv_render();
        assert(osd_rows>0);
        assert(globe_ready()&&drv_frame_failed&&redisplay_posts==posts);
        capture_osd_full();
        assert(strstr(osd_lines[1],"injected traversal failure")!=NULL);
        assert(drv_rot_x==old_x&&drv_rot_y==old_y);
        assert(frame_geometry.vertices==0&&!ogl_texture_used&&!ogl_dlist_used);
    }
    fail_traversal=FALSE;
    posts=redisplay_posts;
    keyboard_callback('a',0,0);
    assert(!drv_auto_enabled&&redisplay_posts==posts+1U);
    keyboard_callback('1',0,0);
    render_checked(FALSE);
    assert(!drv_frame_failed&&!drv_osd_enabled);
    puts("PASS OpenGL failed-frame recovery: OSD-off diagnostics, no automatic redraw loop, and explicit input retry");
}

static void check_cached_frame(int indexed)
{
    unsigned int previous_vertices=emitted_vertices,previous_lists=native_lists;
    render_checked(indexed);
    /* Cached native lists execute again without rebuilding or resubmitting
     * vertex data. The indexed software texture raster is redrawn each time. */
    if(!ogl_dlist_enabled){
        assert(emitted_vertices==previous_vertices+frame_geometry.vertices);
        assert(native_lists==previous_lists&&dlist==0);
    }else if(strcmp(profile,"listfail")==0&&!(indexed&&drv_texture_enabled)){
        assert(emitted_vertices==previous_vertices+frame_geometry.vertices);
        assert(native_lists==0&&dlist==0&&list_attempts>0);
    }else if(ogl_dlist_blocked){
        assert(emitted_vertices==previous_vertices+frame_geometry.vertices);
        assert(native_lists==previous_lists&&dlist==0&&list_deletions==native_lists);
    }else assert(emitted_vertices==previous_vertices&&native_lists==previous_lists);
}


static void check_display_list_toggle(int indexed)
{
    unsigned int level,previous_posts,previous_attempts,previous_calls;
    unsigned int previous_deletions,previous_validations;
    /* A cached list is validated once, after compiling, not before calls. */
    assert(ogl_dlist_enabled&&dlist!=0&&!drv_texture_enabled);
    previous_attempts=list_attempts;previous_validations=list_validations;
    render_checked(indexed);
    assert(ogl_dlist_used&&list_attempts==previous_attempts&&list_validations==previous_validations);
    keyboard_callback('d',0,0);keyboard_callback('D',0,0);
    render_checked(indexed);
    assert(ogl_dlist_used&&dlist!=0&&list_attempts==previous_attempts+1U&&
           list_validations==previous_validations+1U);
    for(level=0;level<5;++level){
        assert(ogl_dlist_enabled&&dlist!=0&&!drv_texture_enabled);
        previous_posts=redisplay_posts;previous_deletions=list_deletions;
        keyboard_callback('d',0,0);
        assert(!ogl_dlist_enabled&&dlist==0);
        assert(redisplay_posts==previous_posts+1U&&list_deletions==previous_deletions+1U);
        previous_attempts=list_attempts;previous_calls=list_calls;
        keyboard_callback((unsigned char)('1'+level),0,0);
        assert(globe_longitude==(32<<level)&&dlist==0);
        render_checked(indexed);check_cached_frame(indexed);
        if(level==0){
            keyboard_callback('f',0,0);assert(!drv_fps_enabled);
            render_checked(indexed);
            keyboard_callback('F',0,0);assert(drv_fps_enabled);
        }
        keyboard_callback('t',0,0);assert(drv_texture_enabled&&dlist==0);
        render_checked(indexed);check_cached_frame(indexed);
        assert(list_attempts==previous_attempts&&list_calls==previous_calls);
        previous_posts=redisplay_posts;
        keyboard_callback('D',0,0);
        assert(ogl_dlist_enabled&&dlist==0&&redisplay_posts==previous_posts+1U);
        assert(list_attempts==previous_attempts&&list_calls==previous_calls);
        render_checked(indexed);check_cached_frame(indexed);
        if(indexed)assert(dlist==0&&list_attempts==previous_attempts&&list_calls==previous_calls);
        else assert(dlist!=0&&list_attempts==previous_attempts+1U);
        keyboard_callback('T',0,0);assert(!drv_texture_enabled&&dlist==0);
        render_checked(indexed);check_cached_frame(indexed);
        assert(dlist!=0);
    }
    puts("PASS OpenGL display-list toggle: default on, d/D redraw, immediate geometry while off, lazy rebuild, resolution/texture changes and ordered OSD status/help");
}

static void check_cull_toggle(int indexed)
{
    GLuint saved;
    unsigned int attempts,calls,posts;
    assert(!drv_cull_enabled&&ogl_dlist_enabled&&!drv_texture_enabled&&dlist!=0);
    saved=dlist;attempts=list_attempts;calls=list_calls;posts=redisplay_posts;
    keyboard_callback('C',0,0);
    assert(drv_cull_enabled&&ogl_dlist_enabled&&redisplay_posts==posts+1U);
    render_checked(indexed);
    assert(dlist==saved&&list_attempts==attempts&&list_calls==calls);
    keyboard_callback('c',0,0);
    render_checked(indexed);
    assert(!drv_cull_enabled&&ogl_dlist_used&&dlist==saved&&list_attempts==attempts);

    /* D changes the saved preference even while C overrides actual use. */
    keyboard_callback('c',0,0);keyboard_callback('d',0,0);
    assert(drv_cull_enabled&&!ogl_dlist_enabled&&dlist==0);
    render_checked(indexed);
    keyboard_callback('C',0,0);render_checked(indexed);
    assert(!drv_cull_enabled&&!ogl_dlist_enabled&&dlist==0);
    keyboard_callback('C',0,0);keyboard_callback('D',0,0);
    attempts=list_attempts;calls=list_calls;
    render_checked(indexed);
    assert(drv_cull_enabled&&ogl_dlist_enabled&&dlist==0&&
           list_attempts==attempts&&list_calls==calls);
    keyboard_callback('c',0,0);render_checked(indexed);
    assert(ogl_dlist_used&&dlist!=0&&list_attempts==attempts+1U);

    /* Native and indexed/software textures follow the same C preference. */
    keyboard_callback('t',0,0);render_checked(indexed);
    keyboard_callback('C',0,0);render_checked(indexed);
    assert(ogl_texture_used&&!ogl_dlist_used);
    keyboard_callback('c',0,0);keyboard_callback('T',0,0);
    render_checked(indexed);
    assert(!drv_cull_enabled&&ogl_dlist_enabled&&ogl_dlist_used);
    puts("PASS OpenGL CPU culling: C/c selects direct band ranges, retained lists stay unused, D preference survives overrides, and flat/native/software texture modes restore correctly");
}

/* Forced CPU culling around the software raster leaves every pixel as the
 * full traversal draws it, and the user's C setting is kept. */
static void check_software_culling(void)
{
    static const float views[6][2]={{0.0f,0.0f},{23.5f,10.0f},{-41.0f,77.0f},
        {89.0f,190.0f},{-89.5f,305.25f},{12.25f,-140.0f}};
    float model[4][4],projection[4][4],rot_x=drv_rot_x,rot_y=drv_rot_y;
    unsigned char *forced;
    unsigned long bytes,culled;
    unsigned int level,view,count;
    int width=192,height=144;
    assert(!drv_cull_enabled&&!drv_texture_enabled);
    ogl_resize(width,height);
    bytes=(unsigned long)width*height;
    forced=(unsigned char *)malloc(bytes);assert(forced);
    keyboard_callback('t',0,0);assert(drv_texture_enabled);
    for(level=0;level<5;++level){
        keyboard_callback((unsigned char)('1'+level),0,0);
        count=level<3U?6U:2U;
        for(view=0;view<count;++view){
            drv_rot_x=views[view][0];drv_rot_y=views[view][1];
            soft_triangles=0;
            drv_render();
            assert(ogl_texture_used&&!drv_cull_enabled&&texsoft_index_pixels()!=NULL);
            culled=soft_triangles;
            memcpy(forced,texsoft_index_pixels(),bytes);
            glGetFloatv(GL_MODELVIEW_MATRIX,&model[0][0]);
            glGetFloatv(GL_PROJECTION_MATRIX,&projection[0][0]);
            assert(texsoft_begin((unsigned int)width,(unsigned int)height,model,projection,
                                 TEXSOFT_INDEX_BYTES,&earth_cmap[0][0]));
            drv_texture_active=TRUE;ogl_draw_globe();drv_texture_active=FALSE;
            assert(soft_triangles-culled==globe_tris_num&&culled<globe_tris_num);
            assert(memcmp(forced,texsoft_index_pixels(),bytes)==0);
        }
    }
    keyboard_callback('T',0,0);
    drv_rot_x=rot_x;drv_rot_y=rot_y;
    ogl_resize(64,48);
    free(forced);
    puts("PASS OpenGL software-texture culling: internal CPU culling skips back faces yet matches full-traversal pixels at all resolutions and views, keeping the C setting");
}

static int creation_failure_profile(void)
{
    return strcmp(profile,"uploadfail")==0||strcmp(profile,"texallocerror")==0||
           strcmp(profile,"uploadinvalid")==0||strncmp(profile,"uploadmixed",11)==0||
           strcmp(profile,"maxzero")==0||strcmp(profile,"maxtiny")==0||
           strcmp(profile,"stagefail")==0||(strncmp(profile,"soft",4)==0&&strcmp(profile,"softreduced")!=0);
}

static void check_texture_failure(int indexed)
{
    unsigned int stages,adoptions,frames,pixel_reads,allocations;
    keyboard_callback('t',0,0);assert(drv_texture_enabled);
    render_checked(indexed);
    assert(ogl_texture_failed&&!ogl_texture_used&&!texture_enabled);
    assert(texture_id==0&&texture_data==NULL&&!drv_texture_loaded);
    assert(frame_geometry.triangles==globe_tris_num);
    if(strcmp(profile,"uploadfail")==0)assert(direct_texture_uploads==6&&capacity_queries==1);
    if(strcmp(profile,"softadoptfail")==0)assert(staging_attempts==6&&soft_adoptions==6);
    if(strcmp(profile,"uploadinvalid")==0||strncmp(profile,"uploadmixed",11)==0)assert(direct_texture_uploads==1&&capacity_queries==1);
    if(strcmp(profile,"maxzero")==0||strcmp(profile,"maxtiny")==0)assert(direct_texture_uploads==0&&staging_attempts==0&&capacity_queries==1);
    stages=staging_attempts;adoptions=soft_adoptions;frames=soft_frame_attempts;
    pixel_reads=pixel_attempts;allocations=texture_allocations;
    render_checked(indexed);
    assert(ogl_texture_failed&&!ogl_texture_used&&drv_texture_enabled);
    assert(staging_attempts==stages&&soft_adoptions==adoptions&&
           soft_frame_attempts==frames&&pixel_attempts==pixel_reads&&texture_allocations==allocations);
    keyboard_callback('T',0,0);assert(!drv_texture_enabled);
    render_checked(indexed);assert(strstr(osd_lines[1],", texture off"));
    texture_injection=0;
    keyboard_callback('t',0,0);assert(drv_texture_enabled);
    render_checked(indexed);
    assert(!ogl_texture_failed&&ogl_texture_used&&drv_texture_loaded);
    assert(strstr(osd_lines[1],indexed?", texture soft":", texture on"));
    keyboard_callback('T',0,0);render_checked(indexed);
    puts("PASS OpenGL recoverable texture failure: short fail/off/soft/on status, complete flat fallback, released resources, latched failure and explicit retry");
}

static int inactive_texture_profile(void)
{return strcmp(profile,"losttexture")==0||strcmp(profile,"bindinglost")==0||strcmp(profile,"disabledtexture")==0||strcmp(profile,"unloadedtexture")==0;}

static void check_inactive_texture(int indexed)
{
    assert(!indexed);
    if(strcmp(profile,"unloadedtexture")==0){
        keyboard_callback('t',0,0);render_checked(indexed);
        assert(ogl_texture_used&&drv_texture_loaded);
        drv_texture_loaded=FALSE;
        render_checked(indexed);
    }else{
        /* The new object, its binding or the enable did not take effect.
         * Enabling checks them once; frames then query no texture state. */
        keyboard_callback('t',0,0);
        assert(!texture_injection&&ogl_texture_failed&&!drv_texture_loaded&&!texture_enabled);
        render_checked(indexed);
    }
    assert(drv_texture_enabled&&ogl_texture_failed&&!ogl_texture_used&&texture_id==0);
    assert(strstr(osd_lines[1],", texture fail"));
    render_checked(indexed);
    keyboard_callback('T',0,0);render_checked(indexed);
    keyboard_callback('t',0,0);render_checked(indexed);
    assert(ogl_texture_used&&!ogl_texture_failed&&strstr(osd_lines[1],", texture on"));
    keyboard_callback('T',0,0);render_checked(indexed);
    puts("PASS OpenGL actual texture use: lost images, changed bindings, disabled native texturing or unloaded state render flat and report fail until retry");
}

static void check_texture_draw_failure(void)
{
    unsigned int clears;
    keyboard_callback('d',0,0);assert(!ogl_dlist_enabled);
    keyboard_callback('t',0,0);assert(drv_texture_enabled&&drv_texture_loaded&&texture_injection);
    clears=frame_clears;
    render_checked(FALSE);
    assert(!texture_injection&&frame_clears==clears+2U);
    assert(ogl_texture_failed&&!ogl_texture_used&&texture_id==0&&!texture_enabled);
    assert(frame_geometry.textured_vertices==0&&frame_geometry.triangles==globe_tris_num);
    assert(strstr(osd_lines[1],", texture fail"));
    render_checked(FALSE);
    keyboard_callback('T',0,0);render_checked(FALSE);
    keyboard_callback('t',0,0);render_checked(FALSE);
    assert(ogl_texture_used&&strstr(osd_lines[1],", texture on"));
    keyboard_callback('T',0,0);keyboard_callback('D',0,0);render_checked(FALSE);
    puts("PASS OpenGL texture draw error: the pre-swap error check redraws that frame flat, latches the failure and allows retry");
}

static void check_list_call_recovery(void)
{
    unsigned int attempts,deletions,clears;
    render_checked(FALSE);
    assert(ogl_dlist_used&&dlist!=0);
    attempts=list_attempts;deletions=list_deletions;clears=frame_clears;
    fail_next_call=1;drv_notice(NULL);
    render_checked(FALSE);
    assert(!ogl_dlist_used&&dlist==0&&list_deletions==deletions+1U&&frame_clears==clears+2U);
    assert(frame_geometry.triangles==globe_tris_num&&strstr(drv_get_notice(),"Display list unavailable")!=NULL);
    render_checked(FALSE);
    assert(!ogl_dlist_used&&list_attempts==attempts);
    keyboard_callback('d',0,0);keyboard_callback('D',0,0);
    render_checked(FALSE);
    assert(ogl_dlist_used&&dlist!=0&&list_attempts==attempts+1U);
    puts("PASS OpenGL display-list call recovery: cached lists are not revalidated per frame; a call error found by the frame's error check redraws directly and D rebuilds the list");
}

int main(int argc,char **argv)
{
    Vertex a,b,c;unsigned int i,level;int indexed;
    char stats[128];
    profile=argc>1?argv[1]:"rgba";
#ifdef TEST_DISPLAY_STRINGS
    glutInitDisplayString("rgba");
#ifdef TEST_OGL_FREEGLUT
    assert((requested_mode&GLUT_ALPHA)==0);
#else
    assert((requested_mode&GLUT_ALPHA)!=0);
#endif
    glutInitDisplayString("rgb");assert((requested_mode&GLUT_ALPHA)==0);
    glutInitDisplayString("rgb alpha>=8");assert((requested_mode&GLUT_ALPHA)!=0);
#endif
    display_string_calls=0;
    if(strcmp(profile,"limited")==0)maximum_texture_size=256;
    matrix_identity(matrices[0][0]);matrix_identity(matrices[1][0]);
    drv_ext_init();drv_init(&argc,argv);assert(!drv_texture_enabled);
    assert(DRV_DLIST_DEFAULT==TRUE&&ogl_dlist_enabled);
    assert(drv_init_window(0,0,64,48));assert(active_window&&windows[active_window].live);
    assert(redisplay_posts>0);
    /* Exact palette indices win over RGB with fewer than 12 bits. */
    indexed=software_profile()||strcmp(profile,"rgb8")==0;
    assert(windows[active_window].indexed==indexed);
    if(indexed)assert(drv_bits_per_pixel==8&&drv_colormapped);
    else if(strcmp(profile,"rgb16")==0)assert(drv_bits_per_pixel==16&&!drv_colormapped);
    else if(strcmp(profile,"rgb8only")==0)assert(drv_bits_per_pixel==8&&drv_colors==256UL&&!drv_colormapped);
#ifndef GLUT_DISPLAY_MODE_POSSIBLE
    else assert(drv_bits_per_pixel==24&&!drv_colormapped);
    assert(window_count==1&&destroy_count==0&&probe_windows==0);
    assert(drv_bytes_per_pixel==(strcmp(profile,"rgb16")==0?2U:3U));
    assert(ogl_has_depth==FALSE&&ogl_double_buffer==TRUE);
#else
    else if(strcmp(profile,"rgb24")==0||strcmp(profile,"unknownalpha")==0)assert(drv_bits_per_pixel==24&&!drv_colormapped);
    else assert(drv_bits_per_pixel==32&&!drv_colormapped);
#endif
    if(strcmp(profile,"rgba-single")==0)assert(!windows[active_window].doubled);
    else assert(windows[active_window].doubled);
#if defined(TEST_OGL_FREEGLUT)
    /* Availability queries choose the request; only used windows exist. */
    assert(probe_windows==0&&display_string_calls==0);
    assert(final_windows==(strcmp(profile,"rgb8")==0?2U:1U)&&destroy_count==(int)final_windows-1);
    if(strcmp(profile,"rgba")==0)assert(mode_queries==1U);
#elif defined(GLUT_DISPLAY_MODE_POSSIBLE)
    assert(final_windows==1);
#ifdef OGL_GLUT_STRINGS
    assert(display_string_calls==25U);
    if(strcmp(profile,"rgba")==0)assert(probe_windows==24U);
#else
    /* Mode bits alone give three request classes, buffering and depth. */
    assert(display_string_calls==0&&probed_count==probe_windows);
    if(strcmp(profile,"rgba")==0)assert(probe_windows==12U);
#endif
#endif
    assert(window_count-destroy_count==1);
    assert(probe_windows==probe_destroys&&!windows[active_window].probe);
    assert(probe_callbacks==probe_windows&&probe_redraws==probe_windows);
    assert(initial_window_redraws>=2U);
    assert(windows[active_window].display!=NULL&&windows[active_window].display!=drv_render);
    assert(reshape_callback&&mouse_callback&&motion_callback&&keyboard_callback);
    if(indexed){assert(palette_calls==256);for(i=0;i<256;++i)assert(palette_mask[i]);}
    a.x=0;a.y=0;a.z=0;b.x=1;b.y=0;b.z=0;c.x=0;c.y=1;c.z=0;
    glBegin(GL_TRIANGLES);drv_draw_tri_flat_color(&a,&b,&c,77);glEnd();
    if(indexed)assert(last_index==77&&indices>0);
    else assert(last_rgb[0]==earth_cmap[77][0]&&last_rgb[1]==earth_cmap[77][1]&&last_rgb[2]==earth_cmap[77][2]);
    globe_init();texture_init();globe_init_verts(32,-1,1000);ogl_resize(64,48);
    if(strcmp(profile,"rgba")==0)check_font_layout();
    posts_before_loop=redisplay_posts;
    drv_loop();assert(loop_calls==1&&windows[active_window].display==drv_render);
    mouse_callback(GLUT_RIGHT_BUTTON,GLUT_DOWN,4,5);assert(drv_rmouse_pressed);
    mouse_callback(GLUT_RIGHT_BUTTON,GLUT_UP,4,5);assert(!drv_rmouse_pressed);
    keyboard_callback('f',0,0);assert(!drv_fps_enabled);
    drv_format_stats(stats);assert(strstr(stats,"frames per second")==NULL);
    keyboard_callback('F',0,0);assert(drv_fps_enabled);
    if(strcmp(profile,"framefail")==0){check_failed_frame();goto cleanup;}
    if(strstr(profile,"reduced")!=NULL){
        keyboard_callback('t',0,0);assert(texture_size==128&&drv_texture_loaded);
        render_checked(indexed);assert(ogl_texture_used&&texture_data==NULL&&staging_attempts<=6U);
        assert(strstr(drv_get_notice(),"128")!=NULL);
        if(strcmp(profile,"uploadreduced")==0)assert(direct_texture_uploads==4&&texture_allocations==4&&texture_deletions==3&&capacity_queries==1);
        drv_notice(NULL);render_checked(indexed);assert(strstr(osd_lines[1],"(128)")!=NULL);
        globe_free();drv_render();assert(!ogl_texture_used&&!ogl_texture_failed&&drv_texture_loaded&&frame_geometry.vertices==0);
        globe_init_verts(32,-1,GLOBE_RADIUS);render_checked(indexed);assert(ogl_texture_used);
        puts("PASS OpenGL reduced texture: bounded allocation downgrade, adopted bilinear software image and empty-scene retry");
        goto cleanup;
    }
    if(creation_failure_profile()){
        check_texture_failure(indexed);
        goto cleanup;
    }
    if(inactive_texture_profile()){
        check_inactive_texture(indexed);
        goto cleanup;
    }
    if(strcmp(profile,"texdrawfail")==0){check_texture_draw_failure();goto cleanup;}
    if(strcmp(profile,"callonce")==0){check_list_call_recovery();goto cleanup;}
    for(level=0;level<5;++level){
        if(level){
            keyboard_callback((unsigned char)('1'+level),0,0);
            assert(globe_longitude==(32<<level));
        }
        render_checked(indexed);check_cached_frame(indexed);
        assert(depth_enabled==(windows[active_window].depth>0));
        if(indexed){unpack_alignment=8;index_shift=2;index_offset=17;map_color=1;zoom_x=2;zoom_y=-1;}
        keyboard_callback('t',0,0);assert(drv_texture_enabled);render_checked(indexed);check_cached_frame(indexed);
        if(indexed)assert(unpack_alignment==8&&index_shift==2&&index_offset==17&&map_color==1&&zoom_x==2&&zoom_y==-1);
        if(indexed)assert(texture_uploads==0);
        else {
            assert(texture_uploads==1&&texture_coordinates>0&&texture_enabled);
            assert(direct_texture_uploads==1&&capacity_queries==1);
        }
        keyboard_callback('T',0,0);assert(!drv_texture_enabled);
#if !TEXTURE_CACHE_RASTER
        assert(texsoft_pixels()==NULL&&texsoft_index_pixels()==NULL);
#else
        if(indexed)assert(texsoft_pixels()==NULL&&texsoft_index_pixels()!=NULL);
#endif
        render_checked(indexed);check_cached_frame(indexed);
    }
    if(indexed)assert(draw_pixels_calls==10);
    if(strcmp(profile,"rgba")==0||strcmp(profile,"legacy-glut")==0||indexed){
        check_display_list_toggle(indexed);
        check_cull_toggle(indexed);
    }
    if(strcmp(profile,"indexed")==0)check_software_culling();
    if(!indexed&&strcmp(profile,"limited")==0)assert(texture_size==256&&direct_texture_uploads==1&&capacity_queries==1);
cleanup:
    assert(stack_depth[0]==0&&stack_depth[1]==0&&attrib_depth==0&&client_depth==0&&cull_enabled);
    if(windows[active_window].doubled)assert(swaps>0);else assert(flushes>0&&swaps==0);
    /* Glyph lists are built at most once per font size and replace every
     * direct glyph unless list storage fails. */
    assert(glyph_block_builds<=3U&&glyph_block_attempts>0);
    if(!glyph_failure_profile())assert(direct_glyphs==0&&call_lists_calls>0&&glyph_block_deletions==0);
    if(strcmp(profile,"fontlistfail")==0)assert(glyph_block_builds==0&&direct_glyphs>0&&call_lists_calls==0);
    if(strcmp(profile,"fontcompilefail")==0)assert(glyph_block_deletions==1U&&direct_glyphs>0&&call_lists_calls>0);
    assert(redundant_colors==0);
    ogl_cleanup();ogl_cleanup();texture_free();globe_free();
    assert(window_count==destroy_count&&active_window==0);
    assert(list_deletions==native_lists&&glyph_block_deletions==glyph_block_builds);
    assert(globe_verts==NULL&&texture_data==NULL);
    puts("PASS OpenGL window lifecycle: safe per-window callbacks through early probe/setup/show redraws, renderer and redisplay activated after globe initialization");
#ifdef GLUT_DISPLAY_MODE_POSSIBLE
    puts("PASS OpenGL format selection: highest verified RGBA/RGB/index precision, buffering/depth fallbacks and unknown alpha handling");
#if defined(TEST_OGL_FREEGLUT)
    puts("PASS FreeGLUT format selection: availability queries walk distinct mode bits in preference order and create only the used window, without display strings");
#elif defined(OGL_GLUT_STRINGS) && GLUT_API_VERSION < 4
    puts("PASS GLUT 3.7 display strings: API version 3 with Xlib implementation 9 or later probes every comparator class");
#elif !defined(OGL_GLUT_STRINGS)
    puts("PASS GLUT mode-bit probes: without display strings each distinct request is probed once and ranked by its actual precision");
#endif
#else
    puts("PASS legacy GLUT window: one RGB/double request at final size and position, no probes or display strings, actual pixel/depth/buffer attributes validated");
#endif
    if(shallow_rgb_profile())puts("PASS OpenGL shallow RGB policy: 8-bit color index replaces RGB with fewer than 12 bits, which remains only without an index visual");
    puts("PASS OpenGL indexed/native rendering: palette bytes, indexed facets, packed CI software pixels and native RGB textures");
    puts("PASS OpenGL retained diagnostics and controls: private error logger, right-button metadata, FPS toggle and software raster cache policy");
    puts("PASS OpenGL synchronous queries: one error check per steady frame before presentation, list validation only after compiling, texture state checked once when enabled");
    puts("PASS OpenGL frame clears: one per frame, none under the full-window software raster, and none repeated while a texture failure is latched");
    puts("PASS OpenGL flat colors: each strip and pole batch sets its first color, then only changes, keeping provoking colors in lists and immediate submissions");
    if(!glyph_failure_profile())puts("PASS OpenGL OSD glyph lists: one list per glyph built once per font size, lines drawn with glCallLists, no per-frame GLUT glyph or width calls, help wrapping cached per font and width");
    if(strcmp(profile,"fontlistfail")==0||strcmp(profile,"fontcompilefail")==0)
        puts("PASS OpenGL OSD glyph fallback: unavailable or failed glyph lists draw the same wrapped text directly");
    if(strcmp(profile,"listfail")==0)puts("PASS OpenGL display-list allocation failure: complete uncached geometry at every resolution without invalid list calls");
    if(strcmp(profile,"newlistfail")==0||strcmp(profile,"listcompilefail")==0)
        puts("PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering");
    if(strcmp(profile,"validationfail")==0||strcmp(profile,"callfail")==0)
        puts("PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail");
    puts("PASS OpenGL display-list OSD: on requires successful current-frame execution, disabled is off, failures and software texturing report fail using OpenGL 1.1 calls");
    puts("PASS OpenGL texture OSD: short on/soft/off/fail labels distinguish actual native or software use, with recoverable failures and complete flat fallback");
    puts("PASS OpenGL bilinear texture policy: base-only GL 1.1 upload, linear minification/magnification at every resolution and display-list state, no mipmap or extension dependency");
#ifdef TRI
    puts("PASS retained OpenGL TRI: native and indexed-texture traversal, five resolutions and display-list caching");
#else
    puts("PASS native OpenGL strips: five resolutions, one combined cap batch, full-band strips, outward winding, provoking colors, UV seams and display-list caching");
#endif
    return 0;
}
