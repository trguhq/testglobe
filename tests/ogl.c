/* Production OpenGL driver with public GL/GLUT call records.
 * Format probing and pixel packing are tested; no visual or GPU is emulated. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/drv_ogl.c"
#include "texsoft.h"

extern Vertex *globe_verts;
extern int globe_longitude;

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
static unsigned int emitted_vertices,texture_coordinates;
static unsigned int matrix_mode=GL_MODELVIEW,stack_depth[2];
static GLfloat matrices[2][16][16];
static GLenum begin_mode;
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

static int available(unsigned int request)
{
    int indexed=(request&GLUT_INDEX)!=0,alpha=(request&GLUT_ALPHA)!=0,doubled=(request&GLUT_DOUBLE)!=0;
    if(strcmp(profile,"nodepth")==0&&(request&GLUT_DEPTH))return 0;
    if(strcmp(profile,"indexed")==0)return indexed;
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
void glutBitmapCharacter(void *font,int c){assert(font&&c>0&&c<256);}
void glutSetColor(int i,GLfloat r,GLfloat g,GLfloat b)
{assert(windows[active_window].indexed&&i>=0&&i<256);assert(fabs(r-(double)earth_cmap[i][0]/255)<1e-6&&fabs(g-(double)earth_cmap[i][1]/255)<1e-6&&fabs(b-(double)earth_cmap[i][2]/255)<1e-6);++palette_calls;palette_mask[i]=1;}
GLenum glGetError(void){return GL_NO_ERROR;}
void glGetIntegerv(GLenum query,GLint *value)
{
    struct Test_window *w=&windows[active_window];assert(w->live);
    switch(query){
    case GL_RED_BITS:*value=w->red;break;case GL_GREEN_BITS:*value=w->green;break;
    case GL_BLUE_BITS:*value=w->blue;break;case GL_ALPHA_BITS:*value=w->alpha;break;
    case GL_INDEX_BITS:*value=w->indexed?8:0;break;case GL_DEPTH_BITS:*value=w->depth;break;
    case GL_RGBA_MODE:*value=!w->indexed;break;case GL_DOUBLEBUFFER:*value=w->doubled;break;
    case GL_MATRIX_MODE:*value=(GLint)matrix_mode;break;
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
void glClear(GLbitfield mask){assert((mask&GL_COLOR_BUFFER_BIT)!=0);assert(((mask&GL_DEPTH_BUFFER_BIT)!=0)==(windows[active_window].depth>0));++frame_clears;}
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
GLuint glGenLists(GLsizei n){assert(n==1);return ++native_lists;}
void glNewList(GLuint l,GLenum mode){assert(l&&mode==GL_COMPILE_AND_EXECUTE);}
void glEndList(void){}
void glCallList(GLuint l){assert(l);}
void glDeleteLists(GLuint l,GLsizei n){assert(l&&n==1);}
void glBegin(GLenum mode){assert(mode==GL_TRIANGLES&&!begin_mode);begin_mode=mode;}
void glEnd(void){assert(begin_mode==GL_TRIANGLES);begin_mode=0;}
void glColor3ub(GLubyte r,GLubyte g,GLubyte b){last_rgb[0]=r;last_rgb[1]=g;last_rgb[2]=b;++rgb_calls;}
void glColor3f(GLfloat r,GLfloat g,GLfloat b){glColor3ub((GLubyte)(r*255),(GLubyte)(g*255),(GLubyte)(b*255));}
void glIndexub(GLubyte i){last_index=i;++indices;}
void glIndexi(GLint i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glIndexf(GLfloat i){assert(i>=0&&i<256);glIndexub((GLubyte)i);}
void glVertex3fv(const GLfloat *v){assert(begin_mode==GL_TRIANGLES&&v);++emitted_vertices;}
void glTexCoord2fv(const GLfloat *v){assert(v&&v[0]>=0&&v[0]<=1&&v[1]>=0&&v[1]<=1);++texture_coordinates;}
void glRasterPos2i(GLint x,GLint y){assert(x>=0&&x<=(int)drv_win_width&&y>=0&&y<=(int)drv_win_height);}
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
}
void glGenTextures(GLsizei n,GLuint *textures){assert(n==1);*textures=1;}
void glDeleteTextures(GLsizei n,const GLuint *textures){assert(n==1&&*textures==1);}
void glBindTexture(GLenum target,GLuint texture){assert(target==GL_TEXTURE_2D&&texture<=1);}
void glTexParameteri(GLenum target,GLenum p,GLint value){assert(target==GL_TEXTURE_2D);assert((p==GL_TEXTURE_MIN_FILTER&&value==GL_LINEAR_MIPMAP_LINEAR)||(p==GL_TEXTURE_MAG_FILTER&&value==GL_LINEAR));}
static void texture_upload(GLsizei w,GLsizei h,const GLvoid *data)
{
    const GLubyte *rgb=data;unsigned long i;
    assert(w==512&&h==256&&rgb);
    for(i=0;i<512UL*256;++i){unsigned int v=earth_data_512[i];assert(rgb[i*3]==earth_cmap[v][0]&&rgb[i*3+1]==earth_cmap[v][1]&&rgb[i*3+2]==earth_cmap[v][2]);}
    ++texture_uploads;
}
void glTexImage2D(GLenum t,GLint l,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum f,GLenum type,const GLvoid *p)
{assert(t==GL_TEXTURE_2D&&l==0&&internal==GL_RGB&&border==0&&f==GL_RGB&&type==GL_UNSIGNED_BYTE);texture_upload(w,h,p);}
void glGenerateMipmap(GLenum t){assert(t==GL_TEXTURE_2D);}
void glFlush(void){++flushes;}
void glShadeModel(GLenum mode){assert(mode==GL_FLAT);}
void glDepthFunc(GLenum function){assert(function==GL_LEQUAL);}
void gluPerspective(GLdouble fov,GLdouble aspect,GLdouble near_plane,GLdouble far_plane)
{GLfloat m[16];double f=1/tan(fov*3.14159265358979323846/360.0);memset(m,0,sizeof(m));m[0]=(GLfloat)(f/aspect);m[5]=(GLfloat)f;m[10]=(GLfloat)((far_plane+near_plane)/(near_plane-far_plane));m[11]=-1;m[14]=(GLfloat)(2*far_plane*near_plane/(near_plane-far_plane));postmultiply(m);}
void gluOrtho2D(GLdouble l,GLdouble r,GLdouble b,GLdouble t)
{GLfloat m[16];matrix_identity(m);m[0]=(GLfloat)(2/(r-l));m[5]=(GLfloat)(2/(t-b));m[10]=-1;m[12]=(GLfloat)(-(r+l)/(r-l));m[13]=(GLfloat)(-(t+b)/(t-b));postmultiply(m);}
GLint gluBuild2DMipmaps(GLenum t,GLint c,GLsizei w,GLsizei h,GLenum f,GLenum type,const GLvoid *p)
{assert(t==GL_TEXTURE_2D&&c==GL_RGB&&f==GL_RGB&&type==GL_UNSIGNED_BYTE);texture_upload(w,h,p);return 0;}

int main(int argc,char **argv)
{
    Vertex a,b,c;unsigned int i;int indexed;
    profile=argc>1?argv[1]:"rgba";
    matrix_identity(matrices[0][0]);matrix_identity(matrices[1][0]);
    drv_ext_init();drv_init(&argc,argv);assert(!drv_texture_enabled);
    assert(drv_init_window(0,0,64,48));assert(active_window&&windows[active_window].live);
    assert(redisplay_posts>0);
    indexed=strcmp(profile,"indexed")==0;
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
    globe_init();texture_init();globe_init_verts(32,-1,1000);drv_resize(64,48);
    posts_before_loop=redisplay_posts;
    drv_loop();assert(loop_calls==1&&windows[active_window].display==drv_render);
    drv_render();assert(emitted_vertices>=globe_tris_num*3UL);assert(depth_enabled==(windows[active_window].depth>0));
    if(indexed){unpack_alignment=8;index_shift=2;index_offset=17;map_color=1;zoom_x=2;zoom_y=-1;}
    keyboard_callback('t',0,0);assert(drv_texture_enabled);drv_render();
    if(indexed)assert(unpack_alignment==8&&index_shift==2&&index_offset==17&&map_color==1&&zoom_x==2&&zoom_y==-1);
    if(indexed)assert(draw_pixels_calls==1&&texture_uploads==0);
    else assert(texture_uploads>0&&texture_coordinates>0&&texture_enabled);
    keyboard_callback('T',0,0);assert(!drv_texture_enabled);drv_render();
    assert(stack_depth[0]==0&&stack_depth[1]==0&&attrib_depth==0&&client_depth==0&&cull_enabled);
    if(windows[active_window].doubled)assert(swaps>0);else assert(flushes>0&&swaps==0);
    ogl_cleanup();ogl_cleanup();
    assert(window_count==destroy_count&&active_window==0);
    assert(globe_verts==NULL&&texture_data==NULL);
    puts("PASS OpenGL window lifecycle: safe per-window callbacks through early probe/setup/show redraws, renderer and redisplay activated after globe initialization");
    puts("PASS OpenGL format selection: highest verified RGBA/RGB/index precision, buffering/depth fallbacks and unknown alpha handling");
    puts("PASS OpenGL indexed/native rendering: palette bytes, indexed facets, packed CI software pixels and native RGB textures");
    return 0;
}
