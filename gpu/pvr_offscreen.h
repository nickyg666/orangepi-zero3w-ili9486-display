// pvr_offscreen.h - PowerVR offscreen GPU render + dmabuf export.
// Renders GLES3 frames on the PowerVR into GPU-owned dmabuf buffers, zero-copy,
// exportable to a compositor (Wayland/zwp_linux_dmabuf_v1) or readable to CPU.
// Build/link: -I. -I/usr/include -L/usr/local/lib -lgbm -lEGL -lGLESv2
#ifndef PVR_OFFSCREEN_H
#define PVR_OFFSCREEN_H
#include <stdint.h>

typedef struct PvrCtx PvrCtx;

/* GBM buffer returned for a rendered frame. */
typedef struct {
    int fd;            /* dmabuf fd for plane 0 */
    int width, height;
    int stride;        /* bytes per row, plane 0 */
    int offset;        /* plane 0 offset */
    int fourcc;        /* DRM fourcc, e.g. XRGB8888 */
    uint64_t modifier; /* DRM modifier */
} PvrBuffer;

/* Create the render context on the PowerVR render node. Returns NULL on error. */
PvrCtx *pvr_create(const char *render_node, int loglevel);
void pvr_destroy(PvrCtx *ctx);

/* Render the current GLES scene into a GPU buffer and hand back its dmabuf.
   width/height must be <= the max allocated when pvr_create_pool was used. */
PvrBuffer pvr_begin_frame(PvrCtx *ctx, int width, int height);
void pvr_end_frame(PvrCtx *ctx, PvrBuffer *buf);

/* Active GL context access for the app to draw (between begin/end). */
void pvr_make_current(PvrCtx *ctx);

const char *pvr_renderer_string(PvrCtx *ctx);

#endif