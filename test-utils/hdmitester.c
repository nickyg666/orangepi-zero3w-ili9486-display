#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

int main(int argc, char**argv){
    const char *dev = argc>1 ? argv[1] : "/dev/dri/card0";
    int fd = open(dev, O_RDWR);
    if(fd<0){perror("open"); return 1;}
    drmSetMaster(fd);
    drmModeRes *res = drmModeGetResources(fd);
    printf("card0: crtcs=%d conns=%d encoders=%d\n", res->count_crtcs, res->count_connectors, res->count_encoders);
    uint32_t conn_id=0, crtc_id=0, encoder_id=0;
    drmModeConnector *conn = NULL;
    for(int i=0;i<res->count_connectors;i++){
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if(conn->connection==DRM_MODE_CONNECTED && conn->connector_type==DRM_MODE_CONNECTOR_HDMIA){
            conn_id = conn->connector_id;
            printf("HDMI connected, %d modes, encoder=%u\n", conn->count_modes, conn->encoder_id);
            encoder_id = conn->encoder_id;
            break;
        }
    }
    if(!conn_id){ printf("no HDMI connected\n"); return 1; }
    /* pick 1280x720 mode */
    drmModeModeInfo *mode=NULL;
    for(int i=0;i<conn->count_modes;i++){
        if(conn->modes[i].hdisplay==1280 && conn->modes[i].vdisplay==720){ mode=&conn->modes[i]; break; }
    }
    if(!mode) mode=&conn->modes[0];
    printf("using mode %s %dx%d\n", mode->name, mode->hdisplay, mode->vdisplay);
    /* find encoder + crtc */
    drmModeEncoder *enc = drmModeGetEncoder(fd, encoder_id);
    crtc_id = enc->crtc_id;
    if(!crtc_id){ /* find free crtc */
        for(int i=0;i<res->count_crtcs;i++){
            drmModeCrtc *c=drmModeGetCrtc(fd,res->crtcs[i]);
            if(!c->buffer_id){ crtc_id=c->crtc_id; drmModeFreeCrtc(c); break; }
            drmModeFreeCrtc(c);
        }
    }
    printf("using crtc %u\n", crtc_id);
    /* create dumb buffer + solid red fb */
    uint32_t w=mode->hdisplay, h=mode->vdisplay;
    struct drm_mode_create_dumb cdb={0};
    cdb.width=w; cdb.height=h; cdb.bpp=32;
    if(drmIoctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&cdb)){perror("create_dumb");return 1;}
    struct drm_mode_map_dumb mmd={0}; mmd.handle=cdb.handle;
    drmIoctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&mmd);
    void *map=mmap(NULL,cdb.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,mmd.offset);
    for(uint32_t i=0;i<w*h;i++){ ((uint32_t*)map)[i]=0x00FF0000; } /* red */
    uint32_t fb_id=0;
    if(drmModeAddFB(fd,w,h,24,32,w*4,cdb.handle,&fb_id)){perror("addfb");return 1;}
    if(drmModeSetCrtc(fd,crtc_id,fb_id,0,0,mode,1,&conn_id)){perror("setcrtc");return 1;}
    printf("SetCrtc OK - RED on HDMI %dx%d\n", w,h);
    sleep(5);
    return 0;
}
