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
typedef int (*PFN_FD)(struct gbm_bo*);
typedef int (*PFN_ST)(struct gbm_bo*);
typedef struct gbm_bo* (*PFN_CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
int main(int argc,char**argv){
  int w=argc>1?atoi(argv[1]):1920, h=argc>2?atoi(argv[2]):1080;
  void *lg=dlopen("libgbm.so.1",RTLD_NOW);
  PFN_FD getfd=(PFN_FD)dlsym(lg,"gbm_bo_get_fd");
  PFN_ST getst=(PFN_ST)dlsym(lg,"gbm_bo_get_stride");
  PFN_CBO cbo=(PFN_CBO)dlsym(lg,"gbm_bo_create");
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
  EGLint pbuf[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  EGLSurface s=eglCreatePbufferSurface(d,cfg,pbuf);
  eglMakeCurrent(d,s,s,c);
  printf("renderer=%s\n",glGetString(GL_RENDERER));
  // extensions needed for render-to-eglimage
  const char*exts=(const char*)glGetString(GL_EXTENSIONS);
  printf("GL_OES_EGL_image: %s\n", strstr(exts,"GL_OES_EGL_image")?"YES":"no");
  printf("GL_OES_EGL_image_external: %s\n", strstr(exts,"GL_OES_EGL_image_external")?"YES":"no");
  PFN_IM createImg=(PFN_IM)eglGetProcAddress("eglCreateImageKHR");
  struct gbm_bo*bo=cbo(gbm,w,h,GBM_FORMAT_XRGB8888,GBM_BO_USE_RENDERING);
  int bfd=getfd(bo); int bst=getst(bo);
  EGLint iattrs[]={
    EGL_LINUX_DRM_FOURCC_EXT,GBM_FORMAT_XRGB8888,
    EGL_WIDTH,w,EGL_HEIGHT,h,
    EGL_DMA_BUF_PLANE0_FD_EXT,bfd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,bst,
    EGL_NONE};
  EGLImage img=createImg(d,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,(EGLClientBuffer)NULL,iattrs);
  printf("img=%p err=%x\n",(void*)img,eglGetError());
  // bind image to texture (GL_OES_EGL_image)
  typedef void (EGLAPIENTRYP PFN_EGII)(GLenum,EGLImageKHR);
  PFN_EGII egii=(PFN_EGII)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  printf("glEGLImageTargetTexture2DOES=%p\n",(void*)egii);
  GLuint tex; glGenTextures(1,&tex);
  glBindTexture(GL_TEXTURE_2D,tex);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  if(egii){ egii(GL_TEXTURE_2D,img); printf("bindimg err=%x\n",glGetError()); }
  GLuint fbo; glGenFramebuffers(1,&fbo);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  printf("fbo status=%x\n",glCheckFramebufferStatus(GL_FRAMEBUFFER));
  glViewport(0,0,w,h);
  glClearColor(0.1,0.9,0.2,1); glClear(GL_COLOR_BUFFER_BIT);
  glFinish();
  printf("render-to-egl-image complete, err=%x\n",glGetError());
  fflush(stdout);
  return 0;
}
