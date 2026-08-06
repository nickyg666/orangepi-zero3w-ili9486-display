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
int main(){
  int fd=open("/dev/dri/card0",O_RDWR);
  if(fd<0){perror("open card0");return 1;}
  struct drm_mode_card_res res; memset(&res,0,sizeof res);
  uint32_t crtcs[8];
  res.crtc_id_ptr=(uint64_t)(uintptr_t)crtcs;
  res.count_crtcs=8;
  if(ioctl(fd,DRM_IOCTL_MODE_GETRESOURCES,&res)<0){perror("getres");return 1;}
  printf("crtcs=%u\n",res.count_crtcs);
  for(int i=0;i<(int)res.count_crtcs;i++){
    struct drm_mode_crtc c; memset(&c,0,sizeof c); c.crtc_id=crtcs[i];
    int r=ioctl(fd,DRM_IOCTL_MODE_GETCRTC,&c);
    if(r<0){printf("crtc%d id=%u: %s\n",i,crtcs[i],strerror(errno));continue;}
    printf("crtc%d id=%u fb_id=%u mode_valid=%d mode=%dx%d@%d\n",i,c.crtc_id,c.fb_id,c.mode_valid,
      c.mode.hdisplay,c.mode.vdisplay,c.mode.vrefresh);
  }
  return 0;
}
