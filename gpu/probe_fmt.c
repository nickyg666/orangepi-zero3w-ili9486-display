#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "gbm.h"
typedef struct gbm_bo* (*CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint64_t (*GMOD)(struct gbm_bo*);
typedef int (*GFD)(struct gbm_bo*);
typedef int (*GST)(struct gbm_bo*);
int main(){
  void*lg=dlopen("libgbm.so.1",RTLD_NOW);
  CBO cbo=(CBO)dlsym(lg,"gbm_bo_create");
  GMOD gmod=(GMOD)dlsym(lg,"gbm_bo_get_modifier");
  GFD gfd=(GFD)dlsym(lg,"gbm_bo_get_fd");
  GST gst=(GST)dlsym(lg,"gbm_bo_get_stride");
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*g=gbm_create_device(fd);
  uint32_t fmts[]={GBM_FORMAT_XRGB8888,GBM_FORMAT_XBGR8888,GBM_FORMAT_ARGB8888,
    GBM_FORMAT_ABGR8888,GBM_FORMAT_BGRX8888,GBM_FORMAT_BGRA8888,
    GBM_FORMAT_RGBX8888,GBM_FORMAT_RGBA8888,0x34324752,0x32474d49};
  const char*nms[]={"XRGB","XBGR","ARGB","ABGR","BGRX","BGRA","RGBX","RGBA","RG24","IMG2"};
  for(int i=0;i<10;i++){
    // try rendering only, then +scanout
    struct gbm_bo*bo=cbo(g,1920,1080,fmts[i],GBM_BO_USE_RENDERING);
    if(bo){
      printf("%-5s RENDERING: ok mod=0x%lx fd=%d st=%d\n",nms[i],(unsigned long)gmod(bo),gfd(bo),gst(bo));
    } else {
      printf("%-5s RENDERING: FAIL\n",nms[i]);
    }
  }
  return 0;
}
