#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"
#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif
static void* p(const char*n){return (void*)eglGetProcAddress(n);}
typedef EGLImageKHR (EGLAPIENTRYP PFN_IM)(EGLDisplay,EGLContext,EGLenum,EGLClientBuffer,const EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFN_Q)(EGLDisplay,EGLImageKHR,int*,int*,EGLuint64KHR*);
typedef EGLBoolean (EGLAPIENTRYP PFN_E)(EGLDisplay,EGLImageKHR,int*,EGLint*,EGLint*);
int main(){
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
  EGLint pbuf[]={EGL_WIDTH,800,EGL_HEIGHT,600,EGL_NONE};
  EGLSurface s=eglCreatePbufferSurface(d,cfg,pbuf);
  eglMakeCurrent(d,s,s,c);
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  PFN_IM createImg=(PFN_IM)p("eglCreateImageKHR");
  PFN_Q query=(PFN_Q)p("eglExportDMABUFImageQueryMESA");
  PFN_E exp=(PFN_E)p("eglExportDMABUFImageMESA");
  // approach A: create gbm bo, render into it via EGL gbm surface? 
  // Instead: use EGL_MESA_drm_image to CREATE a drm image directly from attribs
  typedef EGLImageKHR (EGLAPIENTRYP PFN_CD)(EGLDisplay,const EGLint*);
  PFN_CD cdrm=(PFN_CD)p("eglCreateDRMImageMESA");
  printf("cdrm=%p\n",(void*)cdrm);
  if(cdrm){
    EGLint da[]={EGL_WIDTH,800,EGL_HEIGHT,600,
      EGL_DRM_BUFFER_FORMAT_MESA,EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
      EGL_DRM_BUFFER_USE_MESA,EGL_DRM_BUFFER_USE_SCANOUT_MESA,
      EGL_NONE};
    EGLImage img=cdrm(d,da);
    printf("drmimage=%p err=%x\n",(void*)img,eglGetError());
    if(img){
      int fourcc=0,planes=0; EGLuint64KHR mods[4]={0};
      if(query(d,img,&fourcc,&planes,mods)){printf("query ok fc=%x pl=%d mod=%lx\n",fourcc,planes,(unsigned long)mods[0]);}
      else printf("query fail %x\n",eglGetError());
      int fds[4]={-1,-1,-1,-1}; EGLint st[4]={0},of[4]={0};
      if(exp(d,img,fds,st,of)){printf("EXPORT ok fd=%d st=%d of=%d\n",fds[0],st[0],of[0]);}
      else printf("export fail %x\n",eglGetError());
    }
  }
  fflush(stdout);
  return 0;
}
