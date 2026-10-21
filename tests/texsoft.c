/* Analytical pixel contracts for the shared software texture fallback.
 * Numeric raster words are tested; the host's unsigned-long ABI is not IRIX. */
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include "texsoft.h"

#define BLACK_PIXEL 0xff000000UL
#define RED_PIXEL 0xff0000ffUL
#define GREEN_PIXEL 0xff00ff00UL
#define BLUE_PIXEL 0xffff0000UL
#define WHITE_PIXEL 0xffffffffUL

static float model[4][4], projection[4][4];
static const unsigned char quadrants[12] = {
    255,0,0, 0,255,0,
    0,0,255, 255,255,255
};

static void identity(float matrix[4][4])
{
    int i,j;
    for(i=0;i<4;++i)for(j=0;j<4;++j)matrix[i][j]=(float)(i==j);
}

static void reset_matrices(void)
{
    identity(model); identity(projection);
}

static unsigned long pixel(unsigned int width,unsigned int x,unsigned int y)
{
    return texsoft_pixels()[(unsigned long)y*width+x];
}

static void solid_quad(float left,float bottom,float right,float top,float z,
                        float u,float v,int reverse)
{
    Vertex a,b,c,d;
    Coord uv;
    a.x=left; a.y=bottom; a.z=z;
    b.x=right; b.y=bottom; b.z=z;
    c.x=right; c.y=top; c.z=z;
    d.x=left; d.y=top; d.z=z;
    uv.u=u; uv.v=v;
    if(reverse) {
        texsoft_triangle(&a,&c,&b,&uv,&uv,&uv);
        texsoft_triangle(&a,&d,&c,&uv,&uv,&uv);
    } else {
        texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
        texsoft_triangle(&a,&c,&d,&uv,&uv,&uv);
    }
}

static void textured_quad(float min_u,float min_v,float max_u,float max_v)
{
    Vertex a,b,c,d;
    Coord ua,ub,uc,ud;
    a.x=-1; a.y=-1; a.z=0;
    b.x=1; b.y=-1; b.z=0;
    c.x=1; c.y=1; c.z=0;
    d.x=-1; d.y=1; d.z=0;
    ua.u=min_u; ua.v=min_v;
    ub.u=max_u; ub.v=min_v;
    uc.u=max_u; uc.v=max_v;
    ud.u=min_u; ud.v=max_v;
    texsoft_triangle(&a,&b,&c,&ua,&ub,&uc);
    texsoft_triangle(&a,&c,&d,&ua,&uc,&ud);
}

static void all_pixels(unsigned int width,unsigned int height,unsigned long expected)
{
    unsigned int x,y;
    for(y=0;y<height;++y)for(x=0;x<width;++x)
        assert(pixel(width,x,y)==expected);
}

static void magnification_and_repeat(void)
{
    unsigned long expected[36];
    reset_matrices();
    assert(texsoft_texture(2,2,quadrants));
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    textured_quad(0,0,1,1);
    assert(pixel(6,1,1)==RED_PIXEL);
    assert(pixel(6,4,1)==GREEN_PIXEL);
    assert(pixel(6,1,4)==BLUE_PIXEL);
    assert(pixel(6,4,4)==WHITE_PIXEL);
    /* At UV=(5/12,5/12), bilinear weights are 4:2:2:1. */
    assert(pixel(6,2,2)==(BLACK_PIXEL|142UL|(85UL<<8)|(85UL<<16)));
    memcpy(expected,texsoft_pixels(),sizeof(expected));
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    textured_quad(2,-3,3,-2);
    assert(memcmp(expected,texsoft_pixels(),sizeof(expected))==0);
}

static void minification_and_trilinear(void)
{
    unsigned char checker[48];
    static const unsigned char bands[12]={255,0,0, 255,0,0, 0,0,255, 0,0,255};
    unsigned int x,y,c;
    unsigned long value;
    for(y=0;y<4;++y)for(x=0;x<4;++x)for(c=0;c<3;++c)
        checker[(y*4+x)*3+c]=(unsigned char)(((x+y)&1U)?255:0);
    reset_matrices();
    assert(texsoft_texture(4,4,checker));
    assert(texsoft_begin(2,2,model,projection,0,NULL));
    /* The offset selects black texel centers without mipmaps; heavy
     * minification must instead select the independently known mean. */
    textured_quad(0.125f,0.125f,8.125f,8.125f);
    all_pixels(2,2,0xff808080UL);
    assert(texsoft_texture(4,1,bands));
    assert(texsoft_begin(1,1,model,projection,0,NULL));
    /* One pixel spans sqrt(2) source texels => LOD=1/2. At u=1/8,
     * level 0 is red, level 1 is 3/4 red + 1/4 blue. Their midpoint
     * is 7/8 red + 1/8 blue, independently of raster traversal. */
    textured_quad(-0.0517766953f,0.5f,0.3017766953f,0.5f);
    value=pixel(1,0,0);
    assert((value&255UL)>=222UL&&(value&255UL)<=224UL);
    assert(((value>>8)&255UL)==0);
    assert(((value>>16)&255UL)>=31UL&&((value>>16)&255UL)<=33UL);
}

static void depth_and_culling(void)
{
    reset_matrices();
    assert(texsoft_texture(2,2,quadrants));
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    solid_quad(-1,-1,1,1,0.5f,0.25f,0.25f,0);
    all_pixels(6,6,RED_PIXEL);
    solid_quad(-1,-1,1,1,-0.5f,0.75f,0.25f,0);
    solid_quad(-1,-1,1,1,0.5f,0.25f,0.25f,0);
    all_pixels(6,6,GREEN_PIXEL);
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    solid_quad(-1,-1,1,1,-0.5f,0.75f,0.25f,0);
    solid_quad(-1,-1,1,1,0.5f,0.25f,0.25f,0);
    all_pixels(6,6,GREEN_PIXEL);
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    solid_quad(-1,-1,1,1,0,0.25f,0.25f,1);
    all_pixels(6,6,BLACK_PIXEL);
}

static void frustum_clipping(void)
{
    Vertex a,b,c;
    Coord uv;
    int axis,sign;
    reset_matrices();
    assert(texsoft_texture(2,2,quadrants));
    uv.u=uv.v=0.25f;
    for(axis=0;axis<3;++axis)for(sign=-1;sign<=1;sign+=2) {
        assert(texsoft_begin(8,8,model,projection,0,NULL));
        a.x=-0.25f;a.y=-0.25f;a.z=0;
        b.x=0.25f;b.y=-0.25f;b.z=0;
        c.x=0;c.y=0.25f;c.z=0;
        if(axis==0){a.x+=sign*3;b.x+=sign*3;c.x+=sign*3;}
        if(axis==1){a.y+=sign*3;b.y+=sign*3;c.y+=sign*3;}
        if(axis==2){a.z+=sign*3;b.z+=sign*3;c.z+=sign*3;}
        texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
        all_pixels(8,8,BLACK_PIXEL);
    }
    assert(texsoft_begin(8,8,model,projection,0,NULL));
    a.x=-1;a.y=-1;a.z=-2;
    b.x=1;b.y=-1;b.z=0;
    c.x=0;c.y=1;c.z=0;
    texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
    assert(pixel(8,4,4)==RED_PIXEL);
    assert(pixel(8,1,1)==BLACK_PIXEL);
    assert(texsoft_begin(8,8,model,projection,0,NULL));
    a.x=-4;a.y=-4;a.z=0;
    b.x=4;b.y=-4;b.z=0;
    c.x=0;c.y=4;c.z=0;
    texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
    all_pixels(8,8,RED_PIXEL);
}

static void row_vectors_and_perspective(void)
{
    unsigned int x,y;
    unsigned char ramp[48];
    Vertex a,b,c;
    Coord ua,ub,uc;
    reset_matrices();
    assert(texsoft_texture(2,2,quadrants));
    model[3][0]=0.5f;
    projection[0][0]=0.5f;
    assert(texsoft_begin(16,16,model,projection,0,NULL));
    solid_quad(-0.75f,-0.25f,-0.25f,0.25f,0,0.25f,0.25f,0);
    for(y=0;y<16;++y)for(x=0;x<16;++x)
        assert(pixel(16,x,y)==((x>=7&&x<=8&&y>=6&&y<=9)?RED_PIXEL:BLACK_PIXEL));
    for(y=0;y<4;++y)for(x=0;x<4;++x) {
        ramp[(y*4+x)*3]=(unsigned char)(x*64);
        ramp[(y*4+x)*3+1]=(unsigned char)(y*64);
        ramp[(y*4+x)*3+2]=0;
    }
    assert(texsoft_texture(4,4,ramp));
    reset_matrices();
    memset(projection,0,sizeof(projection));
    projection[0][0]=1;projection[1][1]=1;projection[2][3]=-1;
    assert(texsoft_begin(16,16,model,projection,0,NULL));
    a.x=-1;a.y=-1;a.z=-1;ua.u=0;ua.v=0;
    b.x=2;b.y=-2;b.z=-2;ub.u=1;ub.v=0;
    c.x=-2;c.y=2;c.z=-2;uc.u=0;uc.v=1;
    texsoft_triangle(&a,&b,&c,&ua,&ub,&uc);
    /* Pixel (3,3) has screen weights (9/16,7/32,7/32). Dividing
     * interpolated u/w,v/w by interpolated 1/w gives u=v=7/50;
     * the ramp bilinear sample is round(64*(4*7/50-1/2))=4. */
    assert(pixel(16,3,3)==(BLACK_PIXEL|4UL|(4UL<<8)));
    assert(pixel(16,12,12)==BLACK_PIXEL);
}

static void indexed_and_lifetimes(void)
{
    unsigned char palette[TEXTURE_PALETTE_BYTES];
    static const unsigned char green[3]={0,255,0};
    reset_matrices();
    memset(palette,0,sizeof(palette));
    palette[9*3+1]=255;
    assert(texsoft_texture(1,1,green));
    assert(texsoft_begin(5,7,model,projection,1,palette));
    solid_quad(-1,-1,1,1,0,0,0,0);
    all_pixels(5,7,9UL);
    palette[9*3+1]=0;palette[17*3+1]=255;
    assert(texsoft_begin(5,7,model,projection,1,palette));
    solid_quad(-1,-1,1,1,0,0,0,0);
    all_pixels(5,7,17UL);
    /* Releasing a disabled texture's viewport must preserve its image and
     * palette lookup, so re-enable needs neither source RGB nor new mips. */
    texsoft_release_raster();texsoft_release_raster();
    assert(texsoft_pixels()==NULL);
    solid_quad(-1,-1,1,1,0,0,0,0);
    assert(texsoft_begin(3,4,model,projection,1,palette));
    solid_quad(-1,-1,1,1,0,0,0,0);
    all_pixels(3,4,17UL);
    assert(texsoft_begin(5,7,model,projection,0,NULL));
    solid_quad(-1,-1,1,1,0,0,0,0);
    all_pixels(5,7,GREEN_PIXEL);
    assert(!texsoft_begin(5,7,model,projection,1,NULL));
    assert(!texsoft_texture(3,1,green));
    assert(!texsoft_texture(0,1,green));
    assert(!texsoft_texture(1,1,NULL));
    assert(!texsoft_begin(0,7,model,projection,0,NULL));
    texsoft_free();texsoft_free();
    assert(texsoft_pixels()==NULL);
    assert(!texsoft_begin(1,1,model,projection,0,NULL));
}

static float invalid_float(unsigned int bits)
{
    float result;
    /* The supported graphics platforms and host fixture use IEEE single
     * precision. memcpy avoids aliasing violations while constructing NaN. */
    assert(sizeof(result)==sizeof(bits));
    memcpy(&result,&bits,sizeof(result));
    return result;
}

static void invalid_numeric_inputs(void)
{
    Vertex a,b,c;
    Coord uv;
    float values[2];
    int i;
    values[0]=invalid_float(0x7fc00000U);
    values[1]=invalid_float(0x7f800000U);
    reset_matrices();
    assert(texsoft_texture(2,2,quadrants));
    for(i=0;i<2;++i) {
        assert(texsoft_begin(6,6,model,projection,0,NULL));
        a.x=-1;a.y=-1;a.z=0;b.x=1;b.y=-1;b.z=0;c.x=0;c.y=1;c.z=0;
        uv.u=uv.v=0.25f;
        a.x=values[i];
        texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
        all_pixels(6,6,BLACK_PIXEL);
        a.x=-1;uv.u=values[i];
        texsoft_triangle(&a,&b,&c,&uv,&uv,&uv);
        all_pixels(6,6,BLACK_PIXEL);
        model[0][0]=values[i];
        assert(!texsoft_begin(6,6,model,projection,0,NULL));
        /* A failed frame setup must not leave the previous raster active. */
        solid_quad(-1,-1,1,1,0,0.25f,0.25f,0);
        all_pixels(6,6,BLACK_PIXEL);
        reset_matrices();projection[0][0]=values[i];
        assert(!texsoft_begin(6,6,model,projection,0,NULL));
        reset_matrices();
    }
    assert(texsoft_begin(6,6,model,projection,0,NULL));
    /* Very large finite repeating UVs remain valid without reaching an
     * out-of-range integer conversion in the sampler or LOD calculation. */
    solid_quad(-1,-1,1,1,0,FLT_MAX,-FLT_MAX,0);
    all_pixels(6,6,0xff808080UL);
    texsoft_free();
}

int main(void)
{
    magnification_and_repeat();
    minification_and_trilinear();
    depth_and_culling();
    frustum_clipping();
    row_vectors_and_perspective();
    indexed_and_lifetimes();
    invalid_numeric_inputs();
    puts("PASS Shared software texture pixels: bilinear/repeat, trilinear/mipmaps, perspective-correct UVs and row-vector matrices");
    puts("PASS Shared software visibility/lifetimes: clipping, depth, culling, palette rebuilds, raster release/reuse and invalid numeric input guards");
    return 0;
}
