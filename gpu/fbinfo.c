#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
int main(int argc,char**argv){
  uint32_t fbid=argc>1?strtoul(argv[1],0,0):163;
  int fd=open("/dev/dri/card0",O_RDWR);
  struct drm_mode_fb_cmd2 c; memset(&c,0,sizeof c);
  struct drm_mode_get_fb f; memset(&f,0,sizeof f);
  f.fb_id=fbid;
  if(ioctl(fd,DRM_IOCTL_MODE_GETFB,&f)<0){perror("getfb");return 1;}
  printf("fb_id=%u w=%u h=%u pitch=%u depth=%u bpp=%u\n",f.fb_id,f.width,f.height,f.pitch,f.depth,f.bpp);
  return 0;
}
