/* Production OpenGL driver with public GL/GLUT call records.
 * Native geometry, format probing and pixel packing are tested; no visual
 * or GPU is emulated. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int test_texture_try_generate(int size);
static int test_texsoft_texture(unsigned int width,unsigned int height,const unsigned char *rgb);
static int test_texsoft_begin(unsigned int width,unsigned int height,float model[4][4],float projection[4][4],int indexed,const unsigned char *colormap);
static void *test_transfer_malloc(size_t size);
#define texture_try_generate test_texture_try_generate
#define texsoft_texture test_texsoft_texture
#define texsoft_begin test_texsoft_begin
#define malloc test_transfer_malloc
#include "../src/drv_ogl.c"
#undef malloc
#undef texsoft_begin
#undef texsoft_texture
#undef texture_try_generate
#include "texsoft.h"
extern int texture_try_generate(int size);
extern int texsoft_texture(unsigned int width,unsigned int height,const unsigned char *rgb);
extern int texsoft_begin(unsigned int width,unsigned int height,float model[4][4],float projection[4][4],int indexed,const unsigned char *colormap);

#define TEST_PI 3.14159265358979323846

extern Vertex *globe_verts;
extern int globe_longitude,globe_latitude;

static const char *profile;
static unsigned int requested_mode;
static int active_window, next_window=1, window_count, destroy_count;
static unsigned int probe_windows,probe_destroys,final_windows;
static unsigned int probe_callbacks,probe_redraws,initial_window_redraws;
static struct Test_window {
    int live,indexed,doubled,red,green,blue,alpha,depth,width,height,probe;
    void (*display)(void);
} windows[64];
static int output_width=64,output_height=48;
static unsigned int palette_calls,draw_pixels_calls,texture_uploads,indices,rgb_calls,swaps,flushes;
static unsigned int direct_texture_uploads,glu_mipmap_calls;
static unsigned int texture_allocations,texture_deletions,mipmap_generations;
static unsigned int proc_lookups,version_queries;
static unsigned int staging_attempts,soft_mip_attempts,soft_frame_attempts,transfer_attempts;
static unsigned int texture_exists,texture_binding,frame_texture_pixels;
static int texture_injection=1;
static unsigned int list_attempts,list_deletions,list_calls,list_validations;
static unsigned int frame_list_successes;
static GLenum pending_error;
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
static GLfloat block_polygon[3][3],block_uvs[3][2],first_uvs[2][2],last_uv[2];
static int texture_enabled,depth_enabled,cull_enabled,unpack_alignment=4;
static GLubyte last_index,last_rgb[3];
static int last_clear_index,index_shift,index_offset,map_color;
static GLfloat zoom_x=1,zoom_y=1;
static unsigned int native_lists,redisplay_posts,frame_clears;
static unsigned int posts_before_loop,loop_calls;
static struct Test_attrib {GLbitfield mask;int texture,depth,cull,shift,offset,map;GLfloat x,y;GLubyte index,rgb[3];} attribs[16];
static unsigned int attrib_depth,client_depth;
static int client_alignment[16];
static unsigned int palette_mask[256];
static void (*reshape_callback)(int,int);
static void (*mouse_callback)(int,int,int,int);
static void (*motion_callback)(int,int);
static void (*keyboard_callback)(unsigned char,int,int);
static char osd_lines[2][256];
static unsigned int osd_lengths[2];
static int osd_line=-1;

static int texture_profile(const char *name)
{return texture_injection&&strcmp(profile,name)==0;}
static int software_profile(void)
{return strcmp(profile,"indexed")==0||strncmp(profile,"soft",4)==0;}
static int test_texture_try_generate(int size)
{++staging_attempts;return texture_profile("stagefail")?FALSE:texture_try_generate(size);}
static int test_texsoft_texture(unsigned int w,unsigned int h,const unsigned char *rgb)
{++soft_mip_attempts;return texture_profile("softmipfail")?FALSE:texsoft_texture(w,h,rgb);}
static int test_texsoft_begin(unsigned int w,unsigned int h,float m[4][4],float p[4][4],int indexed,const unsigned char *c)
{++soft_frame_attempts;return texture_profile("softframefail")?FALSE:texsoft_begin(w,h,m,p,indexed,c);}
static void *test_transfer_malloc(size_t size)
{++transfer_attempts;return texture_profile("softtransferallocfail")?NULL:malloc(size);}

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
    case 32:return earth_data_32;case 64:return earth_data_64;
    case 128:return earth_data_128;case 256:return earth_data_256;
    case 512:return earth_data_512;
    }
    assert(!"Unknown globe resolution");return NULL;
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
    if(indexed)return 1;
    if((strcmp(profile,"rgb24")==0||strcmp(profile,"rgb16")==0)&&alpha)return 0;
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
void glutInitDisplayMode(unsigned int mode){requested_mode=mode;}
void glutInitDisplayString(const char *s)
{
    requested_mode=0;
    if(strstr(s,"depth"))requested_mode|=GLUT_DEPTH;
    if(strstr(s,"index"))requested_mode|=GLUT_INDEX;
    if(strstr(s,"double"))requested_mode|=GLUT_DOUBLE;
    if(strstr(s,"alpha"))requested_mode|=GLUT_ALPHA;
}
int glutGet(GLenum query)
{
    struct Test_window *w=&windows[active_window];
    if(query==GLUT_DISPLAY_MODE_POSSIBLE)return available(requested_mode);
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
void glutInitWindowPosition(int x,int y){assert(x>=-32768&&y>=-32768);}
int glutCreateWindow(const char *title)
{
    struct Test_window *w;
    int probe;
    assert(title&&available(requested_mode)&&next_window<64);
    probe=strcmp(title,"testglobe pixel-format probe")==0;
    if(probe) {
        assert(final_windows==0&&output_width==1&&output_height==1);
        ++probe_windows;
    }
    else {
        assert(strcmp(title,TESTGLOBE_NAME)==0&&final_windows==0);
        assert(probe_windows>0&&probe_destroys==probe_windows);
        assert(window_count==destroy_count&&active_window==0);
        assert(output_width==64&&output_height==48);
        ++final_windows;
    }
    active_window=next_window++;w=&windows[active_window];memset(w,0,sizeof(*w));
    w->probe=probe;
    w->live=1;w->indexed=(requested_mode&GLUT_INDEX)!=0;w->doubled=(requested_mode&GLUT_DOUBLE)!=0;
    w->depth=(requested_mode&GLUT_DEPTH)?24:0;w->width=output_width;w->height=output_height;
    if(!w->indexed){w->red=w->green=w->blue=8;w->alpha=(requested_mode&GLUT_ALPHA)?8:0;
        if(strcmp(profile,"unknownalpha")==0&&(requested_mode&GLUT_ALPHA))w->alpha=-1;
        if(strcmp(profile,"rgb16")==0){w->red=5;w->green=6;w->blue=5;w->alpha=0;}}
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
void glutBitmapCharacter(void *font,int c)
{
    assert(font&&c>0&&c<256&&osd_line>=0&&osd_line<2);
    assert(osd_lengths[osd_line]<sizeof(osd_lines[osd_line])-1U);
    osd_lines[osd_line][osd_lengths[osd_line]++]=(char)c;
    osd_lines[osd_line][osd_lengths[osd_line]]=0;
}
void glutSetColor(int i,GLfloat r,GLfloat g,GLfloat b)
{assert(windows[active_window].indexed&&i>=0&&i<256);assert(fabs(r-(double)earth_cmap[i][0]/255)<1e-6&&fabs(g-(double)earth_cmap[i][1]/255)<1e-6&&fabs(b-(double)earth_cmap[i][2]/255)<1e-6);++palette_calls;palette_mask[i]=1;}
GLenum glGetError(void){GLenum code=pending_error;pending_error=GL_NO_ERROR;return code;}
const GLubyte *glGetString(GLenum query)
{
    assert(query==GL_VERSION);++version_queries;
    if(strcmp(profile,"nullversion")==0)return NULL;
    if(strcmp(profile,"badversion")==0)return (const GLubyte *)"unknown";
    return (const GLubyte *)(strcmp(profile,"legacy11")==0?"1.1 test":"3.0 test");
}
void glGetIntegerv(GLenum query,GLint *value)
{
    struct Test_window *w=&windows[active_window];assert(w->live);
    switch(query){
    case GL_RED_BITS:*value=w->red;break;case GL_GREEN_BITS:*value=w->green;break;
    case GL_BLUE_BITS:*value=w->blue;break;case GL_ALPHA_BITS:*value=w->alpha;break;
    case GL_INDEX_BITS:*value=w->indexed?8:0;break;case GL_DEPTH_BITS:*value=w->depth;break;
    case GL_RGBA_MODE:*value=!w->indexed;break;case GL_DOUBLEBUFFER:*value=w->doubled;break;
    case GL_MATRIX_MODE:*value=(GLint)matrix_mode;break;
    case GL_TEXTURE_BINDING_2D:*value=(GLint)texture_binding;break;
    case GL_MAX_TEXTURE_SIZE:*value=strcmp(profile,"maxzero")==0?0:(GLint)maximum_texture_size;break;
    case GL_UNPACK_ALIGNMENT:*value=unpack_alignment;break;
    case GL_INDEX_SHIFT:*value=index_shift;break;case GL_INDEX_OFFSET:*value=index_offset;break;case GL_MAP_COLOR:*value=map_color;break;
    default:assert(!"Unmodeled GL integer query");*value=0;break;
    }
}
void glGetBooleanv(GLenum query,GLboolean *value){GLint i;glGetIntegerv(query,&i);*value=(GLboolean)(i!=0);}
GLboolean glIsEnabled(GLenum capability){if(capability==GL_TEXTURE_2D)return (GLboolean)texture_enabled;if(capability==GL_DEPTH_TEST)return (GLboolean)depth_enabled;if(capability==GL_CULL_FACE)return (GLboolean)cull_enabled;assert(!"Unexpected enabled query");return 0;}
void glPixelTransferi(GLenum parameter,GLint value){if(parameter==GL_INDEX_SHIFT)index_shift=value;else if(parameter==GL_INDEX_OFFSET)index_offset=value;else if(parameter==GL_MAP_COLOR)map_color=value;else assert(!"Unexpected pixel transfer");}
void glPushAttrib(GLbitfield mask)
{struct Test_attrib *a;assert(attrib_depth<16);a=&attribs[attrib_depth++];a->mask=mask;a->texture=texture_enabled;a->depth=depth_enabled;a->cull=cull_enabled;a->shift=index_shift;a->offset=index_offset;a->map=map_color;a->x=zoom_x;a->y=zoom_y;a->index=last_index;memcpy(a->rgb,last_rgb,3);}
void glPopAttrib(void)
{struct Test_attrib *a;assert(attrib_depth);a=&attribs[--attrib_depth];if(a->mask&GL_ENABLE_BIT){texture_enabled=a->texture;depth_enabled=a->depth;cull_enabled=a->cull;}if(a->mask&GL_PIXEL_MODE_BIT){index_shift=a->shift;index_offset=a->offset;map_color=a->map;zoom_x=a->x;zoom_y=a->y;}if(a->mask&GL_CURRENT_BIT){last_index=a->index;memcpy(last_rgb,a->rgb,3);}}
void glPushClientAttrib(GLbitfield mask){assert(mask==GL_CLIENT_PIXEL_STORE_BIT&&client_depth<16);client_alignment[client_depth++]=unpack_alignment;}
void glPopClientAttrib(void){assert(client_depth);unpack_alignment=client_alignment[--client_depth];}
void glTexEnvi(GLenum target,GLenum parameter,GLint value){assert(target==GL_TEXTURE_ENV&&parameter==GL_TEXTURE_ENV_MODE&&value==GL_REPLACE);}
void glGetFloatv(GLenum query,GLfloat *value)
{unsigned int s;if(query==GL_ZOOM_X){*value=zoom_x;return;}if(query==GL_ZOOM_Y){*value=zoom_y;return;}assert(query==GL_MODELVIEW_MATRIX||query==GL_PROJECTION_MATRIX);s=query==GL_PROJECTION_MATRIX?1U:0U;memcpy(value,matrices[s][stack_depth[s]],16*sizeof(*value));}
void glClear(GLbitfield mask){assert((mask&GL_COLOR_BUFFER_BIT)!=0);assert(((mask&GL_DEPTH_BUFFER_BIT)!=0)==(windows[active_window].depth>0));memset(&frame_geometry,0,sizeof(frame_geometry));frame_list_successes=frame_texture_pixels=0;++frame_clears;}
void glClearColor(GLclampf r,GLclampf g,GLclampf b,GLclampf a){assert(r==0&&g==0&&b==0&&a==1);}
void glClearIndex(GLfloat index){assert(index>=0&&index<256);last_clear_index=(int)index;}
void glEnable(GLenum c){if(c==GL_TEXTURE_2D)texture_enabled=1;else if(c==GL_DEPTH_TEST){assert(windows[active_window].depth>0);depth_enabled=1;}else if(c==GL_CULL_FACE)cull_enabled=1;else assert(!"Unexpected glEnable");}
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
GLuint glGenLists(GLsizei n){assert(n==1&&native_lists<63);++list_attempts;if(strcmp(profile,"listfail")==0)return 0;++native_lists;list_reserved[native_lists]=1;return native_lists;}
void glNewList(GLuint l,GLenum mode){assert(l&&l<64&&list_reserved[l]&&mode==GL_COMPILE&&!current_list&&!begin_mode);if(strcmp(profile,"newlistfail")==0){pending_error=GL_OUT_OF_MEMORY;return;}current_list=l;list_exists[l]=1;memset(&list_geometry[l],0,sizeof(list_geometry[l]));}
void glEndList(void){assert(current_list&&!begin_mode);current_list=0;}
GLboolean glIsList(GLuint l){assert(l&&l<64&&list_reserved[l]&&!current_list&&!begin_mode);++list_validations;return strcmp(profile,"validationfail")!=0&&list_exists[l]?GL_TRUE:GL_FALSE;}
void glCallList(GLuint l){assert(l&&l<64&&list_exists[l]&&!list_compile_failed[l]&&!current_list&&!begin_mode);++list_calls;if(strcmp(profile,"callfail")==0){pending_error=GL_INVALID_OPERATION;return;}++frame_list_successes;geometry_add(&frame_geometry,&list_geometry[l]);}
void glDeleteLists(GLuint l,GLsizei n){assert(l&&l<64&&list_reserved[l]&&n==1&&!current_list&&!begin_mode);list_exists[l]=list_reserved[l]=0;++list_deletions;}
void glBegin(GLenum mode){assert((mode==GL_TRIANGLES||mode==GL_TRIANGLE_STRIP)&&!begin_mode);begin_mode=mode;block_vertices=block_coordinates=0;}
void glEnd(void)
{
    struct Test_geometry geometry;
    assert(begin_mode);
    if(globe_verts!=NULL){
        memset(&geometry,0,sizeof(geometry));
        geometry.vertices=block_vertices;
        if(begin_mode==GL_TRIANGLE_STRIP){
            assert(block_vertices==2U*((unsigned int)globe_longitude+1U));
            geometry.triangles=block_vertices-2U;geometry.strip_batches=1;
            if(texture_enabled){
                assert(first_uvs[0][0]==0&&first_uvs[1][0]==0);
                assert(block_uvs[0][0]==1&&block_uvs[1][0]==1);
            }
        }else{
#ifdef TRI
            assert(block_vertices==3UL*globe_tris_num);
#else
            assert(block_vertices==6U*(unsigned int)globe_longitude);
#endif
            geometry.triangles=block_vertices/3U;geometry.cap_batches=1;
        }
        assert(block_coordinates==(texture_enabled?block_vertices:0U));
        if(texture_enabled)geometry.textured_vertices=block_vertices;
        if(current_list){if(!list_compile_failed[current_list])geometry_add(&list_geometry[current_list],&geometry);}
        else geometry_add(&frame_geometry,&geometry);
    }
    begin_mode=0;
}
void glColor3ub(GLubyte r,GLubyte g,GLubyte b){last_rgb[0]=r;last_rgb[1]=g;last_rgb[2]=b;++rgb_calls;}
void glColor3f(GLfloat r,GLfloat g,GLfloat b){glColor3ub((GLubyte)(r*255),(GLubyte)(g*255),(GLubyte)(b*255));}
void glIndexub(GLubyte i){last_index=i;++indices;}
void glIndexi(GLint i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glIndexf(GLfloat i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glVertex3fv(const GLfloat *v)
{
    unsigned int slot;
    double xy,t,u,difference;
    assert(begin_mode&&v);
    if(current_list&&strcmp(profile,"listcompilefail")==0&&!list_compile_failed[current_list]&&block_vertices==8U){
        pending_error=GL_OUT_OF_MEMORY;list_compile_failed[current_list]=1;
    }
    if(globe_verts!=NULL){
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
        assert(y==10||y==(int)drv_win_height-18);
        osd_line=y==10?0:1;
        osd_lengths[osd_line]=0;osd_lines[osd_line][0]=0;
    }
}
void glRasterPos2f(GLfloat x,GLfloat y){assert(x>=-1&&x<=drv_win_width&&y>=-1&&y<=drv_win_height);}
void glPixelStorei(GLenum p,GLint v){if(p==GL_UNPACK_ALIGNMENT){assert(v==1||v==2||v==4||v==8);unpack_alignment=v;}else assert(v==0);}
void glPixelZoom(GLfloat x,GLfloat y){zoom_x=x;zoom_y=y;}
void glDrawPixels(GLsizei w,GLsizei h,GLenum format,GLenum type,const GLvoid *data)
{
    unsigned long i,count;const GLubyte *bytes=data;unsigned long *source=texsoft_pixels();int nonbackground=0;
    assert(w==(int)drv_win_width&&h==(int)drv_win_height&&format==GL_COLOR_INDEX&&type==GL_UNSIGNED_BYTE&&unpack_alignment==1);
    assert(index_shift==0&&index_offset==0&&!map_color&&zoom_x==1&&zoom_y==1);
    assert(bytes&&source);count=(unsigned long)w*h;
    for(i=0;i<count;++i){assert(source[i]<256&&bytes[i]==(GLubyte)source[i]);if(bytes[i]!=last_clear_index)nonbackground=1;}
    assert(nonbackground);++draw_pixels_calls;
    if(texture_profile("softtransfererror")){pending_error=GL_INVALID_OPERATION;return;}
    ++frame_texture_pixels;
}
void glGenTextures(GLsizei n,GLuint *textures){assert(n==1);++texture_allocations;if(texture_profile("texallocerror")){*textures=0;pending_error=GL_OUT_OF_MEMORY;}else *textures=1;}
void glDeleteTextures(GLsizei n,const GLuint *textures){assert(n==1&&*textures==1);texture_exists=0;if(texture_binding==*textures)texture_binding=0;++texture_deletions;}
void glBindTexture(GLenum target,GLuint texture){assert(target==GL_TEXTURE_2D&&texture<=1);texture_binding=texture;if(texture)texture_exists=1;}
GLboolean glIsTexture(GLuint texture){assert(texture<=1);return texture&&texture_exists?GL_TRUE:GL_FALSE;}
void glTexParameteri(GLenum target,GLenum p,GLint value){assert(target==GL_TEXTURE_2D);assert((p==GL_TEXTURE_MIN_FILTER&&value==GL_LINEAR_MIPMAP_LINEAR)||(p==GL_TEXTURE_MAG_FILTER&&value==GL_LINEAR));}
static void texture_upload(GLsizei w,GLsizei h,const GLvoid *data)
{
    const GLubyte *rgb=data;unsigned long i;
    assert(w==512&&h==256&&rgb);
    for(i=0;i<512UL*256;++i){unsigned int v=earth_data_512[i];assert(rgb[i*3]==earth_cmap[v][0]&&rgb[i*3+1]==earth_cmap[v][1]&&rgb[i*3+2]==earth_cmap[v][2]);}
    ++texture_uploads;
}
void glTexImage2D(GLenum t,GLint l,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum f,GLenum type,const GLvoid *p)
{assert(t==GL_TEXTURE_2D&&l==0&&internal==GL_RGB&&border==0&&f==GL_RGB&&type==GL_UNSIGNED_BYTE);assert(w<=(GLsizei)maximum_texture_size&&h<=(GLsizei)maximum_texture_size);++direct_texture_uploads;if(texture_profile("texuploaderror")){pending_error=GL_OUT_OF_MEMORY;return;}texture_upload(w,h,p);}
#ifdef TEST_OGL_FREEGLUT
#include <GL/freeglut_ext.h>
static void test_generate_mipmap(GLenum t){assert(t==GL_TEXTURE_2D);++mipmap_generations;if(texture_profile("mipmaperror"))pending_error=GL_OUT_OF_MEMORY;}
GLUTproc glutGetProcAddress(const char *name){assert(strcmp(name,"glGenerateMipmap")==0);++proc_lookups;return strcmp(profile,"missingproc")==0?NULL:(GLUTproc)test_generate_mipmap;}
#endif
void glFlush(void){++flushes;}
void glShadeModel(GLenum mode){assert(mode==GL_FLAT);}
void glDepthFunc(GLenum function){assert(function==GL_LEQUAL);}
void gluPerspective(GLdouble fov,GLdouble aspect,GLdouble near_plane,GLdouble far_plane)
{GLfloat m[16];double f=1/tan(fov*3.14159265358979323846/360.0);memset(m,0,sizeof(m));m[0]=(GLfloat)(f/aspect);m[5]=(GLfloat)f;m[10]=(GLfloat)((far_plane+near_plane)/(near_plane-far_plane));m[11]=-1;m[14]=(GLfloat)(2*far_plane*near_plane/(near_plane-far_plane));postmultiply(m);}
void gluOrtho2D(GLdouble l,GLdouble r,GLdouble b,GLdouble t)
{GLfloat m[16];matrix_identity(m);m[0]=(GLfloat)(2/(r-l));m[5]=(GLfloat)(2/(t-b));m[10]=-1;m[12]=(GLfloat)(-(r+l)/(r-l));m[13]=(GLfloat)(-(t+b)/(t-b));postmultiply(m);}
GLint gluBuild2DMipmaps(GLenum t,GLint c,GLsizei w,GLsizei h,GLenum f,GLenum type,const GLvoid *p)
{assert(t==GL_TEXTURE_2D&&c==GL_RGB&&f==GL_RGB&&type==GL_UNSIGNED_BYTE);++glu_mipmap_calls;if(texture_profile("gluerror"))return GLU_OUT_OF_MEMORY;texture_upload(w,h,p);return 0;}

static void check_dlist_osd(void)
{
    const char *option,*state,*fps,*texture,*expected_state;
    option=strstr(osd_lines[0],"T = texture mapping, D = display list");
    assert(option&&strcmp(option,"T = texture mapping, D = display list")==0);
    assert(strcmp(osd_lines[0],drv_help)==0);
    expected_state=!ogl_dlist_enabled?", dlist off":
                    (frame_list_successes?", dlist on":", dlist fail");
    state=strstr(osd_lines[1],expected_state);
    assert(state);
    texture=state+strlen(expected_state);
    assert(strcmp(texture,!drv_texture_enabled?", texture off":
           ((frame_geometry.textured_vertices||frame_texture_pixels)?", texture on":", texture fail"))==0);
    assert(strstr(osd_lines[1],"texture mapping")==NULL);
    fps=strstr(osd_lines[1],"frames per second");
    if(drv_fps_enabled){
        assert(fps&&fps<state);
#if !DEBUG
        assert(fps+strlen("frames per second")==state);
#endif
    }else assert(fps==NULL);
}

static void render_checked(int indexed)
{
    unsigned int previous_pixels=draw_pixels_calls;
    unsigned int previous_attempts=list_attempts,previous_calls=list_calls;
    unsigned int previous_validations=list_validations;
    drv_render();
    assert(ogl_dlist_used==(frame_list_successes!=0));
    assert(ogl_texture_used==((frame_geometry.textured_vertices||frame_texture_pixels)!=0));
    if(!ogl_dlist_enabled)
        assert(list_attempts==previous_attempts&&list_calls==previous_calls&&dlist==0);
    if(indexed&&drv_texture_enabled&&!ogl_texture_failed){
        assert(!ogl_dlist_used&&list_attempts==previous_attempts&&
               list_calls==previous_calls&&list_validations==previous_validations);
        assert(draw_pixels_calls==previous_pixels+1U);
        assert(frame_geometry.vertices==0&&frame_geometry.triangles==0);
    }else{
        assert(draw_pixels_calls==previous_pixels||
               (ogl_texture_failed&&draw_pixels_calls==previous_pixels+1U));
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
    if(!ogl_dlist_enabled)assert(list_validations==previous_validations);
    if(ogl_dlist_enabled&&!(indexed&&drv_texture_enabled)&&
       (strcmp(profile,"listfail")==0||strcmp(profile,"newlistfail")==0||
        strcmp(profile,"listcompilefail")==0||strcmp(profile,"validationfail")==0||
        strcmp(profile,"callfail")==0))assert(!ogl_dlist_used&&dlist==0);
    assert(!begin_mode&&!current_list);
    if(drv_texture_enabled&&ogl_texture_failed)assert(frame_geometry.textured_vertices==0&&!frame_texture_pixels);
    if(drv_osd_enabled)check_dlist_osd();
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
    }else if(strcmp(profile,"newlistfail")==0||strcmp(profile,"listcompilefail")==0||
             strcmp(profile,"validationfail")==0||strcmp(profile,"callfail")==0){
        assert(emitted_vertices==previous_vertices+frame_geometry.vertices*
               (strcmp(profile,"newlistfail")==0?1U:2U));
        assert(native_lists==previous_lists+1U&&dlist==0&&list_deletions==native_lists);
    }else assert(emitted_vertices==previous_vertices&&native_lists==previous_lists);
}

static void check_display_list_toggle(int indexed)
{
    unsigned int level,previous_posts,previous_attempts,previous_calls;
    unsigned int previous_deletions;
    /* Simulate a previously defined cache disappearing. The invalid object
     * must not execute, and the next frame must recover with a fresh list. */
    assert(ogl_dlist_enabled&&dlist!=0&&!drv_texture_enabled);
    list_exists[dlist]=0;
    previous_attempts=list_attempts;previous_calls=list_calls;
    previous_deletions=list_deletions;
    render_checked(indexed);
    assert(!ogl_dlist_used&&dlist==0&&list_attempts==previous_attempts&&
           list_calls==previous_calls&&list_deletions==previous_deletions+1U);
    render_checked(indexed);
    assert(ogl_dlist_used&&dlist!=0&&list_attempts==previous_attempts+1U);
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

static int creation_failure_profile(void)
{
    return strcmp(profile,"gluerror")==0||strcmp(profile,"texallocerror")==0||
           strcmp(profile,"texuploaderror")==0||strcmp(profile,"mipmaperror")==0||
           strcmp(profile,"stagefail")==0||strncmp(profile,"soft",4)==0;
}

static void check_texture_failure(int indexed)
{
    unsigned int stages,mips,frames,transfers,allocations;
    keyboard_callback('t',0,0);assert(drv_texture_enabled);
    render_checked(indexed);
    assert(ogl_texture_failed&&!ogl_texture_used&&!texture_enabled);
    assert(texture_id==0&&texture_data==NULL&&!drv_texture_loaded);
    assert(frame_geometry.triangles==globe_tris_num);
    stages=staging_attempts;mips=soft_mip_attempts;frames=soft_frame_attempts;
    transfers=transfer_attempts;allocations=texture_allocations;
    render_checked(indexed);
    assert(ogl_texture_failed&&!ogl_texture_used&&drv_texture_enabled);
    assert(staging_attempts==stages&&soft_mip_attempts==mips&&
           soft_frame_attempts==frames&&transfer_attempts==transfers&&texture_allocations==allocations);
    keyboard_callback('T',0,0);assert(!drv_texture_enabled);
    render_checked(indexed);assert(strstr(osd_lines[1],", texture off"));
    texture_injection=0;
    keyboard_callback('t',0,0);assert(drv_texture_enabled);
    render_checked(indexed);
    assert(!ogl_texture_failed&&ogl_texture_used&&drv_texture_loaded);
    assert(strstr(osd_lines[1],", texture on"));
    keyboard_callback('T',0,0);render_checked(indexed);
    puts("PASS OpenGL recoverable texture failure: short fail/off/on status, complete flat fallback, released resources, latched failure and explicit retry");
}

static int inactive_texture_profile(void)
{return strcmp(profile,"losttexture")==0||strcmp(profile,"bindinglost")==0||strcmp(profile,"disabledtexture")==0||strcmp(profile,"unloadedtexture")==0;}

static void check_inactive_texture(int indexed)
{
    assert(!indexed);
    keyboard_callback('t',0,0);render_checked(indexed);
    assert(ogl_texture_used&&drv_texture_loaded);
    if(strcmp(profile,"losttexture")==0)texture_exists=0;
    else if(strcmp(profile,"bindinglost")==0)texture_binding=0;
    else if(strcmp(profile,"disabledtexture")==0)texture_enabled=0;
    else drv_texture_loaded=FALSE;
    render_checked(indexed);
    assert(drv_texture_enabled&&ogl_texture_failed&&!ogl_texture_used&&texture_id==0);
    assert(strstr(osd_lines[1],", texture fail"));
    render_checked(indexed);
    keyboard_callback('T',0,0);render_checked(indexed);
    keyboard_callback('t',0,0);render_checked(indexed);
    assert(ogl_texture_used&&!ogl_texture_failed&&strstr(osd_lines[1],", texture on"));
    keyboard_callback('T',0,0);render_checked(indexed);
    puts("PASS OpenGL actual texture use: lost images, changed bindings, disabled native texturing or unloaded state render flat and report fail until retry");
}

int main(int argc,char **argv)
{
    Vertex a,b,c;unsigned int i,level;int indexed;
    char stats[128];
    profile=argc>1?argv[1]:"rgba";
    if(strcmp(profile,"limited")==0)maximum_texture_size=256;
    matrix_identity(matrices[0][0]);matrix_identity(matrices[1][0]);
    drv_ext_init();drv_init(&argc,argv);assert(!drv_texture_enabled);
    assert(DRV_DLIST_DEFAULT==TRUE&&ogl_dlist_enabled);
    assert(drv_init_window(0,0,64,48));assert(active_window&&windows[active_window].live);
    assert(redisplay_posts>0);
    indexed=software_profile();
    if(indexed)assert(drv_bits_per_pixel==8&&drv_colormapped);
    else if(strcmp(profile,"rgb16")==0)assert(drv_bits_per_pixel==16&&!drv_colormapped);
    else if(strcmp(profile,"rgb24")==0||strcmp(profile,"unknownalpha")==0)assert(drv_bits_per_pixel==24&&!drv_colormapped);
    else assert(drv_bits_per_pixel==32&&!drv_colormapped);
    if(strcmp(profile,"rgba-single")==0)assert(!windows[active_window].doubled);
    else assert(windows[active_window].doubled);
    assert(window_count-destroy_count==1&&final_windows==1);
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
    posts_before_loop=redisplay_posts;
    drv_loop();assert(loop_calls==1&&windows[active_window].display==drv_render);
    mouse_callback(GLUT_RIGHT_BUTTON,GLUT_DOWN,4,5);assert(drv_rmouse_pressed);
    mouse_callback(GLUT_RIGHT_BUTTON,GLUT_UP,4,5);assert(!drv_rmouse_pressed);
    keyboard_callback('f',0,0);assert(!drv_fps_enabled);
    drv_format_stats(stats);assert(strstr(stats,"frames per second")==NULL);
    keyboard_callback('F',0,0);assert(drv_fps_enabled);
    if(creation_failure_profile()){
        check_texture_failure(indexed);
        goto cleanup;
    }
    if(inactive_texture_profile()){
        check_inactive_texture(indexed);
        goto cleanup;
    }
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
            assert(glu_mipmap_calls+direct_texture_uploads==1);
            if(direct_texture_uploads)assert(mipmap_generations==1&&proc_lookups==1&&version_queries==1);
            else assert(mipmap_generations==0);
        }
        keyboard_callback('T',0,0);assert(!drv_texture_enabled);
#if !TEXTURE_CACHE_RASTER
        assert(texsoft_pixels()==NULL&&ogl_index_pixels==NULL&&ogl_index_capacity==0);
#else
        if(indexed)assert(texsoft_pixels()!=NULL&&ogl_index_pixels!=NULL&&ogl_index_capacity>=64UL*48UL);
#endif
        render_checked(indexed);check_cached_frame(indexed);
    }
    if(indexed)assert(draw_pixels_calls==10);
    if(strcmp(profile,"rgba")==0||indexed)check_display_list_toggle(indexed);
    if(!indexed){
#if !defined(NO_GENMIPMAP) && defined(TEST_OGL_FREEGLUT)
        if(strcmp(profile,"legacy11")==0||strcmp(profile,"badversion")==0||strcmp(profile,"nullversion")==0)
            assert(glu_mipmap_calls==1&&proc_lookups==0&&direct_texture_uploads==0&&version_queries==1);
        if(strcmp(profile,"missingproc")==0)
            assert(glu_mipmap_calls==1&&proc_lookups==1&&direct_texture_uploads==0);
        if(strcmp(profile,"limited")==0||strcmp(profile,"maxzero")==0)
            assert(glu_mipmap_calls==1&&version_queries==0&&proc_lookups==0&&direct_texture_uploads==0);
#else
        assert(glu_mipmap_calls==1&&version_queries==0&&proc_lookups==0&&direct_texture_uploads==0);
#endif
    }
cleanup:
    assert(stack_depth[0]==0&&stack_depth[1]==0&&attrib_depth==0&&client_depth==0&&cull_enabled);
    if(windows[active_window].doubled)assert(swaps>0);else assert(flushes>0&&swaps==0);
    ogl_cleanup();ogl_cleanup();
    assert(window_count==destroy_count&&active_window==0);
    assert(list_deletions==native_lists);
    assert(globe_verts==NULL&&texture_data==NULL);
    puts("PASS OpenGL window lifecycle: safe per-window callbacks through early probe/setup/show redraws, renderer and redisplay activated after globe initialization");
    puts("PASS OpenGL format selection: highest verified RGBA/RGB/index precision, buffering/depth fallbacks and unknown alpha handling");
    puts("PASS OpenGL indexed/native rendering: palette bytes, indexed facets, packed CI software pixels and native RGB textures");
    puts("PASS OpenGL retained diagnostics and controls: private error logger, right-button metadata, FPS toggle and software raster cache policy");
    if(strcmp(profile,"listfail")==0)puts("PASS OpenGL display-list allocation failure: complete uncached geometry at every resolution without invalid list calls");
    if(strcmp(profile,"newlistfail")==0||strcmp(profile,"listcompilefail")==0)
        puts("PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering");
    if(strcmp(profile,"validationfail")==0||strcmp(profile,"callfail")==0)
        puts("PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail");
    puts("PASS OpenGL display-list OSD: on requires successful current-frame execution, disabled is off, failures and software texturing report fail using OpenGL 1.1 calls");
    puts("PASS OpenGL texture OSD: short on/off/fail labels reflect actual native or software use, with recoverable failures and complete flat fallback");
    puts("PASS OpenGL 1.1 compatibility: legacy GLU requires no later GL declarations, optional modern mipmaps require runtime version and resolved entry point");
#ifdef TRI
    puts("PASS retained OpenGL TRI: native and indexed-texture traversal, five resolutions and display-list caching");
#else
    puts("PASS native OpenGL strips: five resolutions, one combined cap batch, full-band strips, outward winding, provoking colors, UV seams and display-list caching");
#endif
    return 0;
}
