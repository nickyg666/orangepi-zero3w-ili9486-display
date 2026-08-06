#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include "gbm.h"
typedef struct gbm_bo* (*CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint64_t (*GMOD)(struct gbm_bo*);
int main(){
  setbuf(stderr,NULL);
  void*lg=dlopen("libgbm.so.1",RTLD_NOW);
  CBO cbo=(CBO)dlsym(lg,"gbm_bo_create");
  GMOD gmod=(GMOD)dlsym(lg,"gbm_bo_get_modifier");
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*g=gbm_create_device(fd);
  fprintf(stderr,"gbm ok\n");
  uint32_t tests[][2]={{GBM_BO_USE_RENDERING,1},{GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR,2},{GBM_BO_USE_LINEAR,3}};
  for(int i=0;i<3;i++){
    fprintf(stderr,"try flag 0x%x\n",tests[i][0]);
    struct gbm_bo*bo=cbo(g,1920,1080,GBM_FORMAT_XRGB8888,tests[i][0]);
    fprintf(stderr,"  -> %s mod=0x%lx\n", bo?"ok":"FAIL", bo?(unsigned long)gmod(bo):0);
  }
  fprintf(stderr,"done\n");
  return 0;
}
