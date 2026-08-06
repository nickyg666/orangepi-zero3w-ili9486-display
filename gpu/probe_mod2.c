#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include "gbm.h"
typedef struct gbm_bo* (*CBO)(struct gbm_device*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef uint64_t (*GMOD)(struct gbm_bo*);
int main(){
  void*lg=dlopen("libgbm.so.1",RTLD_NOW);
  CBO cbo=(CBO)dlsym(lg,"gbm_bo_create");
  GMOD gmod=(GMOD)dlsym(lg,"gbm_bo_get_modifier");
  int fd=open("/dev/dri/renderD129",O_RDWR);
  struct gbm_device*g=gbm_create_device(fd);
  uint32_t flags[]={GBM_BO_USE_RENDERING,GBM_BO_USE_RENDERING|GBM_BO_USE_SCANOUT,
                    GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR};
  const char*n[]={"REND","REND+SCANOUT","REND+LINEAR"};
  for(int i=0;i<3;i++){
    struct gbm_bo*bo=cbo(g,1920,1080,GBM_FORMAT_XRGB8888,flags[i]);
    if(bo) printf("%-14s mod=0x%lx\n",n[i],(unsigned long)gmod(bo));
    else printf("%-14s FAIL\n",n[i]);
  }
  return 0;
}
