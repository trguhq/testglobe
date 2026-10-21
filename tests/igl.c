/* Actual IRIS GL driver, recording native calls against manual declarations.
 * No test here renders pixels or replaces SGI's SDK, libgl, or IRIX ABI. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/drv_igl.c"

extern Vertex *globe_verts;
extern int globe_longitude, globe_latitude;

static int mode_single, mode_indexed, mode_noz, mode_notexture, mode_unknown, mode_affine;
static int mode_rgba, mode_rgba_single, mode_rgb_single, mode_unknown_alpha;
static int window_open, rgb_mode, double_mode, z_enabled, cull_enabled;
static int matrix_mode = MVIEWING, matrix_depth[4], texture_bound, environment_bound;
static Matrix matrices[4], matrix_stack[4][16];
static unsigned long raster_writes;
static unsigned long emitted, drawn, swaps, flushes, clears, zclears, text_calls;
static unsigned long object_polygons[16], next_object = 1, current_object;
static int object_exists[16];
static unsigned long texture_definitions, texture_pixels;
static unsigned long current_color;
static unsigned int polygon_vertices, coordinate_calls, current_uv;
static float polygon[3][3];
static long window_width = 640, window_height = 480;
static long origin_x = 100, origin_y = 100, pointer_x = 120, pointer_y = 130;
static short saved_palette[256][3], actual_palette[256][3];
static unsigned int palette_changes, closed, exits, window_constraints;
#ifdef IGL_TEST_SUBDIVIDE
static int subdivision_enabled;
#endif
static int requested[1024];
static char recorded_stats[256];
static float rotation_values[8];
static char rotation_axes[8];
static int rotations;
static struct {long device;short value;} events[64];
static unsigned int event_count, event_index;

static unsigned char *earth_bytes(void)
{
    switch(globe_longitude) {
    case 32:return earth_data_32;
    case 64:return earth_data_64;
    case 128:return earth_data_128;
    case 256:return earth_data_256;
    case 512:return earth_data_512;
    }
    assert(!"Unknown globe resolution"); return NULL;
}

long getgdesc(long inquiry)
{
    switch(inquiry) {
    case GD_BITS_NORM_DBL_RED: case GD_BITS_NORM_DBL_GREEN:
    case GD_BITS_NORM_DBL_BLUE: return mode_single||mode_indexed ? 0 : (mode_rgb_single ? 5 : 8);
    case GD_BITS_NORM_SNG_RED:case GD_BITS_NORM_SNG_GREEN:
    case GD_BITS_NORM_SNG_BLUE:return mode_indexed ? 0 : 8;
#ifdef GD_BITS_NORM_SNG_ALPHA
    case GD_BITS_NORM_SNG_ALPHA:return mode_unknown_alpha ? -1 : ((mode_rgba||mode_rgba_single) ? 8 : 0);
    case GD_BITS_NORM_DBL_ALPHA:return mode_unknown_alpha ? -1 : (mode_rgba ? 8 : 0);
#endif
    case GD_BITS_NORM_DBL_CMODE:return mode_single ? 0 : 8;
    case GD_BITS_NORM_SNG_CMODE:return 8;
    case GD_BITS_NORM_ZBUFFER:return mode_noz ? 0 : 24;
    case GD_ZMIN:return 0;
    case GD_ZMAX:return 0xffffffL;
    case GD_XPMAX:return 1280;
    case GD_YPMAX:return 1024;
    case GD_TEXTURE:return mode_unknown ? -1 : (mode_notexture ? 0 : 1);
    #ifdef GD_TEXTURE_PERSP
    case GD_TEXTURE_PERSP:return mode_affine ? 0 : 1;
#endif
    case GD_DITHER:return 1;
    default:return -1;
    }
}
void prefsize(long w,long h){assert(w>0&&h>0);window_width=w;window_height=h;}
void prefposition(long l,long r,long b,long t)
{assert(r>=l&&t>=b);origin_x=l;origin_y=b;window_width=r-l+1;window_height=t-b+1;}
long winopen(char *title){assert(!window_open&&strcmp(title,TESTGLOBE_NAME)==0);window_open=1;return 1;}
void winclose(long window){assert(window==1&&window_open);window_open=0;++closed;}
void winconstraints(void){assert(window_open&&window_constraints==0);++window_constraints;}
void gexit(void){++exits;}
void RGBmode(void){assert(!mode_indexed);rgb_mode=1;}
void cmode(void){rgb_mode=0;}
void singlebuffer(void){double_mode=0;}
void doublebuffer(void){assert(!mode_single);double_mode=1;}
void gconfig(void){assert(window_open);}
void getsize(long *w,long *h){assert(window_constraints==1);*w=window_width;*h=window_height;}
void getorigin(long *x,long *y){*x=origin_x;*y=origin_y;}
void reshapeviewport(void){assert(window_open);}
void viewport(Screencoord l,Screencoord r,Screencoord b,Screencoord t)
{assert(l==0&&b==0);assert(r==(int)drv_win_width-1&&t==(int)drv_win_height-1);}
void mmode(short mode){assert(mode>=0&&mode<4);matrix_mode=mode;}
static void identity(Matrix m){int i,j;for(i=0;i<4;++i)for(j=0;j<4;++j)m[i][j]=(float)(i==j);}
static void multiply_left(Matrix left){Matrix out;int i,j,k;for(i=0;i<4;++i)for(j=0;j<4;++j){out[i][j]=0;for(k=0;k<4;++k)out[i][j]+=left[i][k]*matrices[matrix_mode][k][j];}memcpy(matrices[matrix_mode],out,sizeof(out));}
void loadmatrix(Matrix matrix){memcpy(matrices[matrix_mode],matrix,sizeof(Matrix));}
void getmatrix(Matrix matrix){memcpy(matrix,matrices[matrix_mode],sizeof(Matrix));}
void pushmatrix(void){assert(matrix_mode==MVIEWING);assert(matrix_depth[matrix_mode]<16);memcpy(matrix_stack[matrix_mode][matrix_depth[matrix_mode]++],matrices[matrix_mode],sizeof(Matrix));}
void popmatrix(void){assert(matrix_mode==MVIEWING&&matrix_depth[matrix_mode]>0);memcpy(matrices[matrix_mode],matrix_stack[matrix_mode][--matrix_depth[matrix_mode]],sizeof(Matrix));}
void perspective(Angle fov,float aspect,float near_plane,float far_plane)
{
    Matrix p;float focal;int i,j;
    assert(matrix_mode==MVIEWING);assert(fov==450);assert(fabs((double)aspect-(double)drv_win_width/drv_win_height)<1e-5);assert(near_plane==1&&far_plane==10000);
    focal=(float)(1.0/tan((double)fov*PI/3600.0));
    for(i=0;i<4;++i)for(j=0;j<4;++j)p[i][j]=0;
    p[0][0]=focal/aspect;p[1][1]=focal;p[2][2]=-(far_plane+near_plane)/(far_plane-near_plane);p[2][3]=-1;p[3][2]=-2*far_plane*near_plane/(far_plane-near_plane);
    memcpy(matrices[MPROJECTION],p,sizeof(p));
}
void ortho2(float l,float r,float b,float t)
{
    Matrix p;assert(matrix_mode==MVIEWING);assert(l==-0.5f&&b==-0.5f&&r==drv_win_width-0.5f&&t==drv_win_height-0.5f);
    identity(p);p[0][0]=2/(r-l);p[1][1]=2/(t-b);p[2][2]=-1;p[3][0]=-(r+l)/(r-l);p[3][1]=-(t+b)/(t-b);memcpy(matrices[MPROJECTION],p,sizeof(p));
}
void translate(float x,float y,float z){Matrix t;assert(matrix_mode==MVIEWING);assert(x==0&&y==0&&z==-3000);identity(t);t[3][0]=x;t[3][1]=y;t[3][2]=z;multiply_left(t);}
void rot(float angle,char axis)
{
    Matrix r;float c,s;assert(rotations<8);rotation_values[rotations]=angle;rotation_axes[rotations++]=axis;
    c=(float)cos((double)angle*PI/180.0);s=(float)sin((double)angle*PI/180.0);identity(r);
    if(axis=='x'){r[1][1]=c;r[1][2]=s;r[2][1]=-s;r[2][2]=c;}
    else if(axis=='y'){r[0][0]=c;r[0][2]=-s;r[2][0]=s;r[2][2]=c;}
    else {assert(axis=='z');r[0][0]=c;r[0][1]=s;r[1][0]=-s;r[1][1]=c;}
    multiply_left(r);
}
void rotate(Angle angle,char axis){rot((float)angle/10.0f,axis);}
void backface(Boolean enabled){cull_enabled=enabled!=0;}
void zbuffer(Boolean enabled){assert(!enabled||!mode_noz);z_enabled=enabled!=0;}
void zclear(void){assert(!mode_noz);++zclears;}
void czclear(unsigned long background,long depth){assert(!mode_noz&&depth==0xffffffL);assert((background&0xffffffUL)==0);++clears;++zclears;}
void lsetdepth(long near_depth,long far_depth){assert(near_depth==0&&far_depth==0xffffffL);}
void zfunction(long function){assert(function==ZF_LEQUAL);}
void clear(void){assert((current_color&0xffffffUL)==0||!rgb_mode);++clears;}
void shademodel(long model){assert(model==FLAT);}
void subpixel(Boolean enabled){assert(enabled);}
#ifdef IGL_TEST_SUBDIVIDE
void scrsubdivide(long mode,float params[])
{
    if(mode==SS_OFF)subdivision_enabled=0;
    else{assert(mode==SS_DEPTH&&params[0]==0&&params[1]==0&&params[2]==10);subdivision_enabled=1;}
}
#endif
static int software_texture_expected(void)
{
    if(mode_notexture||mode_indexed)return 1;
#ifdef IGL_TEST_SUBDIVIDE
    return 0;
#else
    return mode_affine;
#endif
}
void bgnpolygon(void){assert(!polygon_vertices);assert(cull_enabled);current_uv=0;}
void v3f(float v[3])
{int i;assert(polygon_vertices<3);for(i=0;i<3;++i){assert(v[i]>=-1000.1f&&v[i]<=1000.1f);polygon[polygon_vertices][i]=v[i];}++polygon_vertices;}
void endpolygon(void)
{
    double ux,uy,uz,vx,vy,vz,cx,cy,cz,area,phi,theta;
    unsigned int latitude,longitude,index;
    unsigned long expected;
    assert(polygon_vertices==3);
    ux=polygon[1][0]-polygon[0][0];uy=polygon[1][1]-polygon[0][1];uz=polygon[1][2]-polygon[0][2];
    vx=polygon[2][0]-polygon[0][0];vy=polygon[2][1]-polygon[0][1];vz=polygon[2][2]-polygon[0][2];
    cx=(polygon[0][0]+polygon[1][0]+polygon[2][0])/3.0;
    cy=(polygon[0][1]+polygon[1][1]+polygon[2][1])/3.0;
    cz=(polygon[0][2]+polygon[1][2]+polygon[2][2])/3.0;
    area=(uy*vz-uz*vy)*cx+(uz*vx-ux*vz)*cy+(ux*vy-uy*vx)*cz;
    assert(area>0.0);
    if(!drv_texture_enabled) {
        phi=atan2(cy,cx);if(phi<0)phi+=2.0*PI;
        theta=atan2(sqrt(cx*cx+cy*cy),cz);
        longitude=(unsigned int)(phi*globe_longitude/(2.0*PI));
        latitude=(unsigned int)(theta*globe_latitude/PI);
        assert(longitude<(unsigned int)globe_longitude&&latitude<(unsigned int)globe_latitude);
        index=earth_bytes()[(unsigned long)latitude*globe_longitude+longitude];
        expected=(unsigned long)earth_cmap[index][0]|((unsigned long)earth_cmap[index][1]<<8)|((unsigned long)earth_cmap[index][2]<<16);
        if(mode_indexed) {
            assert(current_color<256);
            assert(current_color==index);
            assert(actual_palette[current_color][0]==(short)(expected&255UL));
            assert(actual_palette[current_color][1]==(short)((expected>>8)&255UL));
            assert(actual_palette[current_color][2]==(short)((expected>>16)&255UL));
        }
        else assert((current_color&0xffffffUL)==expected);
    }
    if(texture_bound)assert(current_uv==3);
    ++emitted;
    if(current_object)++object_polygons[current_object];else ++drawn;
    polygon_vertices=0;
}
void c3f(float c[3]){current_color=(unsigned long)(c[0]*255.0f+0.5f)|((unsigned long)(c[1]*255.0f+0.5f)<<8)|((unsigned long)(c[2]*255.0f+0.5f)<<16);}
void c3s(short c[3]){RGBcolor(c[0],c[1],c[2]);}
void cpack(unsigned long packed){assert((packed&0xff000000UL)==0xff000000UL);current_color=packed;}
void RGBcolor(short r,short g,short b){assert(r>=0&&r<=255&&g>=0&&g<=255&&b>=0&&b<=255);current_color=(unsigned long)r|((unsigned long)g<<8)|((unsigned long)b<<16);}
void color(Colorindex i){assert(i<256);current_color=(unsigned long)i;}
void mapcolor(Colorindex i,short r,short g,short b){assert(i<256);actual_palette[i][0]=r;actual_palette[i][1]=g;actual_palette[i][2]=b;++palette_changes;}
void getmcolor(Colorindex i,short *r,short *g,short *b){assert(i<256);*r=actual_palette[i][0];*g=actual_palette[i][1];*b=actual_palette[i][2];}
void texdef2d(long id,long components,long width,long height,unsigned long *image,long np,float *props)
{
    unsigned long i,pixel;
    unsigned int index;
    assert(!software_texture_expected()&&id>0&&components==4&&width==512&&height==256);
    assert(image!=NULL&&np>0&&props[np-1]==TX_NULL);
    for(i=0;i<(unsigned long)width*height;++i){
        index=earth_data_512[i];pixel=(unsigned long)earth_cmap[index][0]|((unsigned long)earth_cmap[index][1]<<8)|((unsigned long)earth_cmap[index][2]<<16)|0xff000000UL;
        assert(image[i]==pixel);
    }
    ++texture_definitions;texture_pixels+=(unsigned long)width*height;
}
void texbind(long target,long id){assert(target==TX_TEXTURE_0);assert(id==0||(!software_texture_expected()));texture_bound=id!=0;}
void tevdef(long id,long np,float *props){assert(id>0&&np>0&&props[np-1]==TV_NULL);}
void tevbind(long target,long id){assert(target==TV_ENV0);environment_bound=id!=0;}
void t2f(float uv[2]){assert(uv[0]>=0&&uv[0]<=1&&uv[1]>=0&&uv[1]<=1);++coordinate_calls;++current_uv;}
long getplanes(void){return mode_indexed?8:24;}
long getheight(void){return 14;}
long strwidth(char *text){return (long)strlen(text)*8;}
void lrectwrite(Screencoord l,Screencoord b,Screencoord r,Screencoord top,unsigned long *pixels)
{
    unsigned long count,i,changed;assert(!z_enabled&&l==0&&b==0&&r==(int)drv_win_width-1&&top==(int)drv_win_height-1&&pixels!=NULL);
    count=(unsigned long)drv_win_width*drv_win_height;changed=0;
    for(i=0;i<count;++i){if(mode_indexed)assert(pixels[i]<256);else assert((pixels[i]&0xff000000UL)==0xff000000UL);if(pixels[i]!=pixels[0])++changed;}
    assert(changed>100);++raster_writes;
}
void font(short index){assert(index==0);}
void cmov2i(Icoord x,Icoord y){assert(x>=0&&x<(long)drv_win_width&&y>=0&&y<(long)drv_win_height);}
void charstr(char *text){
#ifdef IGL_TEST_SUBDIVIDE
assert(!subdivision_enabled);
#endif
assert(!texture_bound&&!z_enabled);assert(text!=NULL);if(strstr(text,"triangles")){strncpy(recorded_stats,text,sizeof(recorded_stats)-1);}++text_calls;}
void qdevice(Device device){assert(device<1024);requested[device]=1;}
void qreset(void){event_index=event_count;}
long qtest(void){return event_index<event_count?events[event_index].device:0;}
long qread(short *value){assert(event_index<event_count);*value=events[event_index].value;return events[event_index++].device;}
long getvaluator(Device device){assert(device==MOUSEX||device==MOUSEY);return device==MOUSEX?pointer_x:pointer_y;}
Boolean getbutton(Device device){assert(device==LEFTMOUSE);return drv_lmouse_pressed;}
void tie(Device button,Device first,Device second){assert(button==LEFTMOUSE&&first==MOUSEX&&second==MOUSEY);}
void swapbuffers(void){assert(double_mode);++swaps;}
void gflush(void){++flushes;}
void frontbuffer(Boolean enabled){assert(enabled==!double_mode);}
void backbuffer(Boolean enabled){assert(enabled==double_mode);}
long genobj(void){assert(next_object<16);return (long)next_object++;}
void makeobj(long id){assert(id>0&&id<16&&!current_object);current_object=(unsigned long)id;object_exists[id]=1;object_polygons[id]=0;}
void closeobj(void){assert(current_object);current_object=0;}
void callobj(long id){assert(id>0&&id<16&&object_exists[id]);drawn+=object_polygons[id];}
void delobj(long id){assert(id>0&&id<16&&object_exists[id]);object_exists[id]=0;}
Boolean isobj(long id){return id>0&&id<16&&object_exists[id];}

static void enqueue(long device,short value){assert(event_count<64);events[event_count].device=device;events[event_count++].value=value;}
static void frame(void)
{
    unsigned long before,old_writes;
    before=drawn;old_writes=raster_writes;rotations=0;drv_render();
    assert(drawn>before||raster_writes>old_writes);
    assert(rotations==4);
    assert(rotation_axes[0]=='x'&&rotation_values[0]==drv_rot_x);
    assert(rotation_axes[1]=='y'&&rotation_values[1]==drv_rot_y);
    assert(rotation_axes[2]=='x'&&rotation_values[2]==-90);
    assert(rotation_axes[3]=='z'&&rotation_values[3]==-90);
    assert(matrix_depth[MVIEWING]==0&&matrix_depth[MPROJECTION]==0);
    assert(cull_enabled&&z_enabled==!mode_noz);
#ifdef IGL_TEST_SUBDIVIDE
    assert(!mode_affine||subdivision_enabled==(drv_texture_enabled!=0));
#endif
    {
        double sx,cx,sy,cy,expected[3][3];int i,j;
        sx=sin((double)drv_rot_x*PI/180);cx=cos((double)drv_rot_x*PI/180);
        sy=sin((double)drv_rot_y*PI/180);cy=cos((double)drv_rot_y*PI/180);
        expected[0][0]=sy;expected[0][1]=-sx*cy;expected[0][2]=cx*cy;
        expected[1][0]=cy;expected[1][1]=sx*sy;expected[1][2]=-cx*sy;
        expected[2][0]=0;expected[2][1]=cx;expected[2][2]=sx;
        for(i=0;i<3;++i)for(j=0;j<3;++j)assert(fabs(matrices[MVIEWING][i][j]-expected[i][j])<1e-5);
        assert(matrices[MVIEWING][3][0]==0&&matrices[MVIEWING][3][1]==0&&matrices[MVIEWING][3][2]==-3000);
        assert(fabs(matrices[MPROJECTION][1][1]-1/tan(PI/8))<1e-5);
        assert(matrices[MPROJECTION][2][3]==-1&&matrices[MPROJECTION][3][3]==0);
    }
    assert(strstr(recorded_stats,"triangles")!=NULL&&strstr(recorded_stats,"quads")==NULL);
}

int main(int argc,char **argv)
{
    int i,j;
    unsigned long before;
    float old_x,old_y;
    mode_rgba=argc>1&&strcmp(argv[1],"rgba")==0;
    mode_rgba_single=argc>1&&strcmp(argv[1],"rgba-single")==0;
    mode_rgb_single=argc>1&&strcmp(argv[1],"rgb-single")==0;
    mode_unknown_alpha=argc>1&&strcmp(argv[1],"unknown-alpha")==0;
    mode_single=argc>1&&(strcmp(argv[1],"single")==0||strcmp(argv[1],"indexed-single")==0);
    mode_indexed=argc>1&&(strcmp(argv[1],"indexed")==0||strcmp(argv[1],"indexed-single")==0);
    mode_noz=argc>1&&strcmp(argv[1],"noz")==0;
    mode_affine=argc>1&&strcmp(argv[1],"affine")==0;
#ifdef IGL_TEST_OLD_HEADERS
    mode_affine=1;
#endif
    mode_unknown=argc>1&&strcmp(argv[1],"unknown")==0;
    mode_notexture=mode_unknown||(argc>1&&strcmp(argv[1],"software")==0);
    for(i=0;i<4;++i)identity(matrices[i]);
    for(i=0;i<256;++i)for(j=0;j<3;++j)actual_palette[i][j]=saved_palette[i][j]=(short)((i+j*37)&255);
    drv_ext_init();drv_init(&argc,argv);assert(!drv_texture_enabled);
    assert(drv_init_window(0,0,0,0));
    assert(window_open&&double_mode==!(mode_single||mode_rgba_single||mode_rgb_single)&&rgb_mode==!mode_indexed);
    assert(drv_bits_per_pixel==(mode_indexed?8U:((mode_rgba||mode_rgba_single)?32U:24U)));
    assert(drv_colormapped==mode_indexed);
    assert(sizeof(Color)==1);
    assert(requested[KEYBD]&&requested[LEFTMOUSE]&&requested[MOUSEX]&&requested[MOUSEY]&&requested[REDRAW]&&requested[INPUTCHANGE]&&requested[WINQUIT]);
    globe_init();texture_init();globe_init_verts(GLOBE_DEFAULT_LONGITUDE,-1,GLOBE_RADIUS);
    assert(globe_longitude==512);
    frame();assert(drawn==globe_tris_num);
    before=emitted;frame();assert(emitted==before);
    for(i='1';i<='5';++i){igl_keyboard((unsigned char)i);assert(globe_longitude==(32<<(i-'1')));frame();}
    igl_keyboard('1');frame();
    old_x=drv_rot_x;old_y=drv_rot_y;
    enqueue(MOUSEX,140);enqueue(MOUSEY,150);igl_event(LEFTMOUSE,1);
    assert(drv_lmouse_pressed);
    igl_event(MOUSEX,170);igl_event(MOUSEY,175);
    {
        double expected_x,expected_y;
        expected_x=old_x-25.0*100.0/drv_win_height;
        expected_y=old_y+30.0*100.0/drv_win_width;
        if(expected_x<0)expected_x+=360;
        if(expected_x>=360)expected_x-=360;
        if(expected_y<0)expected_y+=360;
        if(expected_y>=360)expected_y-=360;
        assert(fabs(drv_rot_x-expected_x)<1e-4&&fabs(drv_rot_y-expected_y)<1e-4);
    }
    enqueue(MOUSEX,170);enqueue(MOUSEY,175);igl_event(LEFTMOUSE,0);assert(!drv_lmouse_pressed);
    before=text_calls;igl_keyboard('o');frame();assert(text_calls==before);igl_keyboard('O');frame();
    igl_keyboard('T');assert(drv_texture_enabled);frame();
    if(!software_texture_expected())assert(texture_definitions==1&&coordinate_calls>0&&texture_pixels==512UL*256UL);
    else assert(texture_definitions==0);
    igl_keyboard('t');assert(!drv_texture_enabled);frame();
    igl_keyboard('a');assert(drv_auto_enabled);igl_keyboard('A');assert(!drv_auto_enabled);
    window_width=500;window_height=300;igl_event(REDRAW,2);assert(drv_win_width==640&&drv_win_height==480);igl_event(REDRAW,1);assert(drv_win_width==500&&drv_win_height==300);frame();
    igl_event(INPUTCHANGE,0);assert(!drv_lmouse_pressed&&!igl_active);igl_event(INPUTCHANGE,1);assert(igl_active);
    igl_event(QFULL,0);assert(event_index==event_count);
    before=drawn;enqueue(KEYBD,'2');enqueue(REDRAW,1);enqueue(WINQUIT,2);
    if(argc>1&&strcmp(argv[1],"winquit")==0)enqueue(WINQUIT,1);
    else if(argc>1&&strcmp(argv[1],"winshut")==0)enqueue(WINSHUT,1);
    else enqueue(KEYBD,27);
    drv_loop();assert(!igl_running&&globe_verts==NULL&&drawn==before);
    assert(event_index==event_count);

    /* Native shutdown is idempotent and restores any shared color-map entries. */
    igl_cleanup();igl_cleanup();
    assert(closed==1&&window_open==0);
    for(i=1;i<16;++i)assert(!object_exists[i]);
    if(mode_indexed){assert(palette_changes>=512);assert(memcmp(saved_palette,actual_palette,sizeof(saved_palette))==0);}
    assert(texture_data==NULL);
    assert(globe_verts==NULL);
    puts("PASS IRIS GL rendering: five indexed LODs, independent native triangle colors/winding, transforms, depth/cull, texture packing and OSD state");
    puts("PASS IRIS GL input/capabilities: native queue, keys, tied mouse, resize, focus, buffering and texture fallbacks");
    puts("PASS IRIS GL cleanup: objects, resources, window and shared palette restored");
    return 0;
}
