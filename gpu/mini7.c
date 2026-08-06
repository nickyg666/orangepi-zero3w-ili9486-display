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
typedef EGLImageKHR (EGLAPIENTRYP PFN_IM)(EGLDisplay,EGLContext,EGLenum,EGLClientBuffer,const EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFN_Q)(EGLDisplay,EGLImageKHR,int*,int*,EGLuint64KHR*);
typedef EGLBoolean (EGLAPIENTRYP PFN_E)(EGLDisplay,EGLImageKHR,int*,EGLint*,EGLint*);
typedef struct gbm_bo* (*PFN_LFB)(struct gbm_surface*);
typedef void (*PFN_RFB)(struct gbm_surface*,struct gbm_bo*);
typedef int (*PFN_FD)(struct gbm_bo*);
typedef int (*PFN_ST)(struct gbm_bo*);
typedef struct gbm_bo* (*PFN_CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
int main(){
  void *lg=dlopen("libgbm.so.1",RTLD_NOW);
  PFN_LFB lfb=(PFN_LFB)dlsym(lg,"gbm_surface_lock_front_buffer");
  PFN_RFB rfb=(PFN_RFB)dlsym(lg,"gbm_surface_release_buffer");
  PFN_FD getfd=(PFN_FD)dlsym(lg,"gbm_bo_get_fd");
  PFN_ST getst=(PFN_ST)dlsym(lg,"gbm_bo_get_stride");
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
  // create gbm bo, import as EGL image (dma-buf import direction), export back
  PFN_CBO cbo=(PFN_CBO)dlsym(lg,"gbm_bo_create");
  struct gbm_bo*bo=cbo(gbm,64,64,GBM_FORMAT_XRGB8888,GBM_BO_USE_RENDERING);
  printf("bo=%p\n",(void*)bo);
  if(!bo) return 1;
  int bfd=getfd(bo); int bst=getst(bo);
  printf("bo fd=%d stride=%d\n",bfd,bst);
  PFN_IM createImg=(PFN_IM)eglGetProcAddress("eglCreateImageKHR");
  PFN_Q query=(PFN_Q)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
  PFN_E exp=(PFN_E)eglGetProcAddress("eglExportDMABUFImageMESA");
  EGLint iattrs[]={
    EGL_LINUX_DRM_FOURCC_EXT, GBM_FORMAT_XRGB8888,
    EGL_WIDTH,64,EGL_HEIGHT,64,
    EGL_DMA_BUF_PLANE0_FD_EXT,bfd,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT,bst,
    EGL_NONE
  };
  EGLImage img=createImg(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,(EGLClientBuffer)NULL,iattrs);
  printf("imported image=%p err=%x\n",(void*)img,eglGetError());
  if(img){
    int fourcc=0,planes=0; EGLuint64KHR mods[4]={0};
    if(query(d,img,&fourcc,&planes,mods)){printf("query ok fc=%x pl=%d\n",fourcc,planes);}
    else printf("query fail %x\n",eglGetError());
    int fds[4]={-1,-1,-1,-1}; EGLint st[4]={0},of[4]={0};
    if(exp(d,img,fds,st,of)){printf("EXPORT ok fd=%d st=%d of=%d\n",fds[0],st[0],of[0]);}
    else printf("export fail %x\n",eglGetError());
  }
  fflush(stdout);
  return 0;
}
