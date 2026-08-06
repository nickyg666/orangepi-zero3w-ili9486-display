#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include "gbm.h"
typedef struct gbm_bo* (*CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint64_t (*GMOD)(struct gbm_bo*);
typedef int (*GFD)(struct gbm_bo*);
typedef int (*GST)(struct gbm_bo*);
typedef uint32_t (*GFMT)(struct gbm_bo*);
int main(int argc,char**argv){
  const char*node=argc>1?argv[1]:"/dev/dri/renderD129";
  uint32_t fmt=argc>2?strtoul(argv[2],0,0):GBM_FORMAT_XRGB8888;
  void*lg=dlopen("libgbm.so.1",RTLD_NOW);
  CBO cbo=(CBO)dlsym(lg,"gbm_bo_create");
  GMOD gmod=(GMOD)dlsym(lg,"gbm_bo_get_modifier");
  GFD gfd=(GFD)dlsym(lg,"gbm_bo_get_fd");
  GST gst=(GST)dlsym(lg,"gbm_bo_get_stride");
  GFMT gf=(GFMT)dlsym(lg,"gbm_bo_get_format");
  int fd=open(node,O_RDWR);
  struct gbm_device*g=gbm_create_device(fd);
  struct gbm_bo*bo=cbo(g,1920,1080,fmt,GBM_BO_USE_RENDERING|GBM_BO_USE_SCANOUT);
  if(!bo){printf("create fail fmt=0x%x\n",fmt);return 1;}
  printf("fmt=0x%x modifier=0x%lx fd=%d stride=%d\n",fmt,(unsigned long)gmod(bo),gfd(bo),gst(bo));
  return 0;
}
