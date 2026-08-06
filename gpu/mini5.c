#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"
#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif
typedef struct gbm_bo* (*PFN_LFB)(struct gbm_surface*);
typedef void (*PFN_RFB)(struct gbm_surface*,struct gbm_bo*);
typedef int (*PFN_FD)(struct gbm_bo*);
typedef int (*PFN_ST)(struct gbm_bo*);
int main(){
  void *lg=dlopen("libgbm.so.1",RTLD_NOW);
  PFN_LFB lfb=(PFN_LFB)dlsym(lg,"gbm_surface_lock_front_buffer");
  PFN_RFB rfb=(PFN_RFB)dlsym(lg,"gbm_surface_release_buffer");
  PFN_FD getfd=(PFN_FD)dlsym(lg,"gbm_bo_get_fd");
  PFN_ST getst=(PFN_ST)dlsym(lg,"gbm_bo_get_stride");
  printf("lfb=%p rfb=%p fd=%p st=%p\n",(void*)lfb,(void*)rfb,(void*)getfd,(void*)getst);
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*gbm=gbm_create_device(fd);
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA,gbm,NULL);
  int mj,mi; eglInitialize(d,&mj,&mi);
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; int n;
  eglBindAPI(EGL_OPENGL_ES_API);
  eglChooseConfig(d,attrs,&cfg,1,&n);
  EGLint ctxa[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxa);
  struct gbm_surface *gs=gbm_surface_create(gbm,800,600,GBM_FORMAT_XRGB8888,GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
  printf("gbm_surface=%p\n",(void*)gs);
  EGLSurface es=eglCreatePlatformWindowSurface(d,cfg,gs,NULL);
  printf("egl surface=%p err=%x\n",(void*)es,eglGetError());
  if(es==EGL_NO_SURFACE) return 1;
  eglMakeCurrent(d,es,es,c);
  printf("current err=%x\n",eglGetError());
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  glClearColor(0.2,0.4,0.9,1); glClear(GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(d,es);
  printf("swapped err=%x\n",eglGetError());
  struct gbm_bo*bo=lfb(gs);
  printf("front bo=%p\n",(void*)bo);
  if(bo){
    printf("bo fd=%d stride=%d w=%d h=%d\n",getfd(bo),getst(bo),gbm_bo_get_width(bo),gbm_bo_get_height(bo));
    int f=getfd(bo);
    char cbuf[4096]; ssize_t r=pread(f,cbuf,sizeof cbuf,0);
    printf("pread fd=%d -> %zd bytes first px=%02x%02x%02x%02x\n",f,r,(unsigned char)cbuf[0],(unsigned char)cbuf[1],(unsigned char)cbuf[2],(unsigned char)cbuf[3]);
    rfb(gs,bo);
  }
  fflush(stdout);
  return 0;
}
