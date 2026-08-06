#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"
#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif
int main(){
  int fd=open("/dev/dri/renderD129",O_RDWR);
  if(fd<0){perror("open");return 1;}
  struct gbm_device*gbm=gbm_create_device(fd);
  printf("gbm=%p\n",(void*)gbm);
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA,gbm,NULL);
  printf("d=%p err=%x\n",(void*)d,eglGetError());
  int mj,mi; if(!eglInitialize(d,&mj,&mi)){printf("init fail %x\n",eglGetError());return 1;}
  printf("init %d.%d\n",mj,mi);
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
  EGLConfig cfg; int n;
  eglBindAPI(EGL_OPENGL_ES_API);
  if(!eglChooseConfig(d,attrs,&cfg,1,&n)){printf("cfg fail %x\n",eglGetError());return 1;}
  EGLint ctxa[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ctxa);
  printf("ctx=%p err=%x\n",(void*)c,eglGetError());
  EGLint pbuf[]={EGL_WIDTH,800,EGL_HEIGHT,600,EGL_NONE};
  EGLSurface s=eglCreatePbufferSurface(d,cfg,pbuf);
  printf("surf=%p err=%x\n",(void*)s,eglGetError());
  eglMakeCurrent(d,s,s,c);
  printf("current err=%x\n",eglGetError());
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  fflush(stdout);
  return 0;
}
