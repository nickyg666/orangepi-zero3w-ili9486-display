// pvr_offscreen.c - PowerVR offscreen GPU render + dmabuf export.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <inttypes.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"
#include "pvr_offscreen.h"

#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif

typedef EGLImageKHR (EGLAPIENTRYP PFN_IMG)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint*);
typedef EGLBoolean (EGLAPIENTRYP PFN_Q)(EGLDisplay, EGLImageKHR, int*, int*, EGLuint64KHR*);
typedef EGLBoolean (EGLAPIENTRYP PFN_E)(EGLDisplay, EGLImageKHR, int*, EGLint*, EGLint*);
typedef void (EGLAPIENTRYP PFN_EGII)(GLenum, EGLImageKHR);

typedef struct gbm_bo* (*GBM_CREATE_BO)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int (*GBM_BO_GET_FD)(struct gbm_bo*);
typedef int (*GBM_BO_GET_STRIDE)(struct gbm_bo*);
typedef uint32_t (*GBM_BO_GET_WIDTH)(struct gbm_bo*);
typedef uint32_t (*GBM_BO_GET_HEIGHT)(struct gbm_bo*);
typedef void (*GBM_BO_DESTROY)(struct gbm_bo*);

struct PvrCtx {
    int fd;
    struct gbm_device *gbm;
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface surf;

    PFN_IMG createImg;
    PFN_Q query;
    PFN_E export_;
    PFN_EGII egii;

    GBM_CREATE_BO gbm_create_bo;
    GBM_BO_GET_FD gbm_bo_get_fd;
    GBM_BO_GET_STRIDE gbm_bo_get_stride;
    GBM_BO_GET_WIDTH gbm_bo_get_width;
    GBM_BO_GET_HEIGHT gbm_bo_get_height;
    GBM_BO_DESTROY gbm_bo_destroy;

    int loglevel;
};

static void logp(PvrCtx *ctx, int lvl, const char *fmt, ...) {
    if (ctx && lvl > ctx->loglevel) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

PvrCtx *pvr_create(const char *render_node, int loglevel) {
    if (!render_node) render_node = "/dev/dri/renderD129";
    PvrCtx *ctx = calloc(1, sizeof *ctx);
    ctx->loglevel = loglevel;

    ctx->fd = open(render_node, O_RDWR);
    if (ctx->fd < 0) { perror("pvr open render node"); free(ctx); return NULL; }
    ctx->gbm = gbm_create_device(ctx->fd);
    if (!ctx->gbm) { fprintf(stderr, "gbm_create_device fail\n"); free(ctx); return NULL; }

    ctx->dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, ctx->gbm, NULL);
    if (ctx->dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetPlatformDisplay fail 0x%x\n", eglGetError()); return NULL; }
    EGLint maj, min;
    if (!eglInitialize(ctx->dpy, &maj, &min)) { fprintf(stderr, "eglInitialize fail\n"); return NULL; }
    logp(ctx, 0, "pvr: EGL %s on %s\n", eglQueryString(ctx->dpy, EGL_VERSION), render_node);

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint n;
    if (!eglChooseConfig(ctx->dpy, attrs, &ctx->cfg, 1, &n) || n < 1) {
        fprintf(stderr, "eglChooseConfig fail\n"); return NULL;
    }
    EGLint ctxattrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    ctx->ctx = eglCreateContext(ctx->dpy, ctx->cfg, EGL_NO_CONTEXT, ctxattrs);
    if (ctx->ctx == EGL_NO_CONTEXT) { fprintf(stderr, "eglCreateContext fail\n"); return NULL; }
    EGLint pbuf[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    ctx->surf = eglCreatePbufferSurface(ctx->dpy, ctx->cfg, pbuf);
    if (ctx->surf == EGL_NO_SURFACE) { fprintf(stderr, "eglCreatePbufferSurface fail\n"); return NULL; }

    ctx->createImg = (PFN_IMG)eglGetProcAddress("eglCreateImageKHR");
    ctx->query = (PFN_Q)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    ctx->export_ = (PFN_E)eglGetProcAddress("eglExportDMABUFImageMESA");
    ctx->egii = (PFN_EGII)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!ctx->createImg || !ctx->query || !ctx->export_ || !ctx->egii) {
        fprintf(stderr, "pvr: missing proc addrs im=%p q=%p e=%p egii=%p\n",
                (void*)ctx->createImg, (void*)ctx->query, (void*)ctx->export_, (void*)ctx->egii);
        return NULL;
    }

    void *lg = dlopen("libgbm.so.1", RTLD_NOW);
    ctx->gbm_create_bo = (GBM_CREATE_BO)dlsym(lg, "gbm_bo_create");
    ctx->gbm_bo_get_fd = (GBM_BO_GET_FD)dlsym(lg, "gbm_bo_get_fd");
    ctx->gbm_bo_get_stride = (GBM_BO_GET_STRIDE)dlsym(lg, "gbm_bo_get_stride");
    ctx->gbm_bo_get_width = (GBM_BO_GET_WIDTH)dlsym(lg, "gbm_bo_get_width");
    ctx->gbm_bo_get_height = (GBM_BO_GET_HEIGHT)dlsym(lg, "gbm_bo_get_height");
    ctx->gbm_bo_destroy = (GBM_BO_DESTROY)dlsym(lg, "gbm_bo_destroy");
    if (!ctx->gbm_create_bo || !ctx->gbm_bo_get_fd || !ctx->gbm_bo_get_stride) {
        fprintf(stderr, "pvr: missing gbm funcs\n"); return NULL;
    }

    pvr_make_current(ctx);
    logp(ctx, 0, "pvr: GL renderer: %s\n", glGetString(GL_RENDERER));
    logp(ctx, 0, "pvr: GL version: %s\n", glGetString(GL_VERSION));
    return ctx;
}

void pvr_destroy(PvrCtx *ctx) {
    if (!ctx) return;
    eglDestroyContext(ctx->dpy, ctx->ctx);
    eglDestroySurface(ctx->dpy, ctx->surf);
    eglTerminate(ctx->dpy);
    if (ctx->gbm) gbm_device_destroy(ctx->gbm);
    if (ctx->fd >= 0) close(ctx->fd);
    free(ctx);
}

void pvr_make_current(PvrCtx *ctx) {
    eglMakeCurrent(ctx->dpy, ctx->surf, ctx->surf, ctx->ctx);
}

const char *pvr_renderer_string(PvrCtx *ctx) {
    return (const char*)glGetString(GL_RENDERER);
}

PvrBuffer pvr_begin_frame(PvrCtx *ctx, int width, int height) {
    PvrBuffer out = {0};
    out.fd = -1;
    struct gbm_bo *bo = ctx->gbm_create_bo(ctx->gbm, width, height,
                                            GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!bo) { fprintf(stderr, "gbm_bo_create fail\n"); return out; }
    out.fd = ctx->gbm_bo_get_fd(bo);
    out.width = ctx->gbm_bo_get_width(bo);
    out.height = ctx->gbm_bo_get_height(bo);
    out.stride = ctx->gbm_bo_get_stride(bo);
    out.fourcc = GBM_FORMAT_XRGB8888;
    out.offset = 0;

    EGLint iattrs[] = {
        EGL_LINUX_DRM_FOURCC_EXT, GBM_FORMAT_XRGB8888,
        EGL_WIDTH, width, EGL_HEIGHT, height,
        EGL_DMA_BUF_PLANE0_FD_EXT, out.fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, out.stride,
        EGL_NONE
    };
    EGLImage img = ctx->createImg(ctx->dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                  (EGLClientBuffer)NULL, iattrs);
    if (img == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "eglCreateImageKHR(dmabuf import) fail 0x%x\n", eglGetError());
        out.fd = -1; return out;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ctx->egii(GL_TEXTURE_2D, img);
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete with EGL image target\n");
        out.fd = -1; return out;
    }
    glViewport(0, 0, width, height);
    return out;
}

void pvr_end_frame(PvrCtx *ctx, PvrBuffer *buf) {
    glFinish();
    (void)ctx;
}
