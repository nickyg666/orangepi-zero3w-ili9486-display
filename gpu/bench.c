// bench.c - measure PowerVR offscreen GPU render FPS at max resolution via pvr_offscreen.
// Build:
//   gcc -O2 -o bench bench.c libpvr_offscreen.a -I. -I/usr/include -L/usr/local/lib \
//       -lgbm -lEGL -lGLESv2 -Wl,-rpath,/usr/local/lib
// Run (needs access to renderD129):
//   LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri ./bench 1920 1080 300
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "pvr_offscreen.h"

static double now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec/1e9; }

static const char *VS =
  "attribute vec2 aPos;\n"
  "varying vec4 vColor;\n"
  "uniform float uT;\n"
  "void main(){\n"
  "  gl_Position=vec4(aPos,0.0,1.0);\n"
  "  float x=0.5*(aPos.x+1.0); float y=0.5*(aPos.y+1.0);\n"
  "  float k=sin((x*20.0+uT))*cos((y*20.0+uT*0.7));\n"
  "  vColor=vec4(x,y,0.5+0.5*k,1.0);\n"
  "}";
static const char *FS =
  "precision mediump float;\n"
  "varying vec4 vColor;\n"
  "void main(){ gl_FragColor=vColor; }";
static const float verts[] = {
  -1,-1,  1,-1,  -1,1,
  -1,1,   1,-1,   1,1,
};

static GLuint prog;
static GLint upos, utT;

int main(int argc,char**argv){
  int w=argc>1?atoi(argv[1]):1920, h=argc>2?atoi(argv[2]):1080;
  int frames=argc>3?atoi(argv[3]):300;
  PvrCtx*ctx=pvr_create("/dev/dri/renderD129",2);
  if(!ctx){fprintf(stderr,"ctx fail\n");return 1;}
  printf("renderer: %s\n",pvr_renderer_string(ctx));

  const char *src[2]={VS,FS};
  char log[512]; GLint ok=0;
  GLuint vs=glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs,1,&VS,NULL); glCompileShader(vs);
  GLuint fs=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs,1,&FS,NULL); glCompileShader(fs);
  prog=glCreateProgram(); glAttachShader(prog,vs); glAttachShader(prog,fs); glLinkProgram(prog);
  glGetProgramiv(prog,GL_LINK_STATUS,&ok);
  if(!ok){ glGetProgramInfoLog(prog,sizeof log,NULL,log); fprintf(stderr,"link: %s\n",log); return 1;}
  upos=glGetAttribLocation(prog,"aPos");
  utT=glGetUniformLocation(prog,"uT");
  glUseProgram(prog);
  glEnableVertexAttribArray(upos);
  GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof verts,verts,GL_STATIC_DRAW);
  glVertexAttribPointer(upos,2,GL_FLOAT,GL_FALSE,0,0);

  /* warmup: first frame establishes FBO/state; exclude it from timing */
  PvrBuffer bw=pvr_begin_frame(ctx,w,h);
  glDrawArrays(GL_TRIANGLES,0,6);
  pvr_end_frame(ctx,&bw);
  glFlush();

  double t0=now();
  for(long i=0;i<frames;i++){
    PvrBuffer b=pvr_begin_frame(ctx,w,h);
    if(b.fd<0){fprintf(stderr,"frame %ld: no buffer\n",i);break;}
    glUniform1f(utT,(float)i/10.0f);
    glDrawArrays(GL_TRIANGLES,0,6);
    pvr_end_frame(ctx,&b);
  }
  double t1=now();
  double dt=t1-t0;
  printf("Rendered %d frames at %dx%d in %.3fs\n",frames,w,h,dt);
  printf("Steady-state FPS: %.1f\n", frames/dt);
  pvr_destroy(ctx);
  return 0;
}