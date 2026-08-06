#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"
#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif
int main(int argc,char**argv){
  int w=argc>1?atoi(argv[1]):320, h=argc>2?atoi(argv[2]):240;
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*gbm=gbm_create_device(fd);
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA,gbm,NULL);
  int mj,mi; eglInitialize(d,&mj,&mi);
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; int n;
  eglBindAPI(EGL_OPENGL_ES_API);
  eglChooseConfig(d,attrs,&cfg,1,&n);
  EGLint ctxa[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxa);
  EGLint pbuf[]={EGL_WIDTH,w,EGL_HEIGHT,h,EGL_NONE};
  EGLSurface s=eglCreatePbufferSurface(d,cfg,pbuf);
  eglMakeCurrent(d,s,s,c);
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  GLuint tex,fbo;
  glGenTextures(1,&tex);
  glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
  glGenFramebuffers(1,&fbo);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  glViewport(0,0,w,h);
  glClearColor(0.9,0.2,0.1,1); glClear(GL_COLOR_BUFFER_BIT);
  glFinish();
  unsigned char *px=malloc(w*h*4);
  glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,px);
  printf("readback err=%x first px=%02x%02x%02x%02x\n",glGetError(),px[0],px[1],px[2],px[3]);
  fflush(stdout);
  return 0;
}
