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
static void* p(const char*n){return (void*)eglGetProcAddress(n);}
int main(){
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*gbm=gbm_create_device(fd);
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA,gbm,NULL);
  int mj,mi; eglInitialize(d,&mj,&mi);
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; int n;
  eglBindAPI(EGL_OPENGL_ES_API);
  eglChooseConfig(d,attrs,&cfg,1,&n);
  printf("config n=%d\n",n);
  EGLint ctxa[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxa);
  // create a gbm surface (scalable multi-buffer)
  struct gbm_surface *gs=gbm_surface_create(gbm,800,600,GBM_FORMAT_XRGB8888,GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
  printf("gbm_surface=%p\n",(void*)gs);
  // EGL window surface from gbm surface
  EGLSurface es=eglCreatePlatformWindowSurface(d,cfg,gs,NULL);
  printf("egl window surface=%p err=%x\n",(void*)es,eglGetError());
  if(es==EGL_NO_SURFACE) return 1;
  eglMakeCurrent(d,es,es,c);
  printf("current err=%x\n",eglGetError());
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  glClearColor(0.2,0.4,0.9,1); glClear(GL_COLOR_BUFFER_BIT);
  // lock front buffer via gbm_surface_lock_front_buffer
  typedef struct gbm_bo* (*PFN_LFB)(struct gbm_surface*);
  typedef void (*PFN_RFB)(struct gbm_surface*,struct gbm_bo*);
  PFN_LFB lfb=(PFN_LFB)p("gbm_surface_lock_front_buffer");
  PFN_RFB rfb=(PFN_RFB)p("gbm_surface_release_buffer");
  eglSwapBuffers(d,es);
  struct gbm_bo*bo=lfb(gs);
  printf("front bo=%p\n",(void*)bo);
  if(bo){
    typedef int (*PFN_FD)(struct gbm_bo*);
    typedef int (*PFN_ST)(struct gbm_bo*);
    PFN_FD getfd=(PFN_FD)p("gbm_bo_get_fd");
    PFN_ST getst=(PFN_ST)p("gbm_bo_get_stride");
    printf("bo fd=%d stride=%d w=%d h=%d\n",
      getfd?getfd(bo):-1, getst?getst(bo):-1, gbm_bo_get_width(bo), gbm_bo_get_height(bo));
    // read pixels to verify GPU actually rendered
    typedef uint32_t (*PFN_MAP)(struct gbm_bo*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,void**);
    void*mapdata=NULL; uint32_t mapst;
    if(getfd){
      int f=getfd(bo);
      char cbuf[8192]; ssize_t r=pread(f,cbuf,sizeof cbuf,0);
      printf("pread fd=%d -> %zd bytes first=%02x%02x%02x%02x\n",f,r,(unsigned char)cbuf[0],(unsigned char)cbuf[1],(unsigned char)cbuf[2],(unsigned char)cbuf[3]);
    }
    rfb(gs,bo);
  }
  fflush(stdout);
  return 0;
}
