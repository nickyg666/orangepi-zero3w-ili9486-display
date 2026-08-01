#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
int main(int argc, char**argv) {
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    drmModeRes *res = drmModeGetResources(fd);
    printf("res fbs=%d crtcs=%d conns=%d\n", res->count_fbs, res->count_crtcs, res->count_connectors);
    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *c = drmModeGetCrtc(fd, res->crtcs[i]);
        printf("CRTC %d: fb=%u mode_valid=%d mode=%s (%ux%u)\n",
            res->crtcs[i], c->buffer_id, c->mode_valid,
            c->mode_valid ? c->mode.name : "-", c->mode.hdisplay, c->mode.vdisplay);
        drmModeFreeCrtc(c);
    }
    drmModeFreeResources(res);
    close(fd);
    return 0;
}
