// gldmabuf.c - render on the PowerVR GPU (renderD128) via GBM + surfaceless EGL,
// then export the rendered EGLImage as a dma-buf fd (EGL_MESA_image_dma_buf_export).
// The SPI panel (renderD129/ili9486) is NEVER used for rendering; we only render on
// the PowerVR and the resulting dma-buf frame can be copied to the SPI panel later.
// Build:
//   gcc -o gldmabuf gldmabuf.c -I. -I/usr/include -L/usr/local/lib \
//       -lgbm -lEGL -lGLESv2 -Wl,-rpath,/usr/local/lib
// Run:
//   LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri ./gldmabuf 1280 720
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "gbm.h"

#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif

int main(int argc, char** argv) {
    int w = argc > 1 ? atoi(argv[1]) : 1280;
    int h = argc > 2 ? atoi(argv[2]) : 720;
    /* 1800000.gpu is the PowerVR; the SPI panel is renderD128. */
    const char *node = getenv("GPU_DRM_NODE");
    if (!node) node = "/dev/dri/renderD129";

    int fd = open(node, O_RDWR);
    if (fd < 0) { perror("open gpu node"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) { fprintf(stderr, "gbm_create_device fail for %s\n", node); return 1; }

    EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint ctxattrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLint pbuf[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
    EGLint major, minor, n;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ps;
    EGLDisplay d;

    d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm, NULL);
    if (d == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetPlatformDisplay(gbm) fail 0x%x\n", eglGetError());
        return 1;
    }
    if (!eglInitialize(d, &major, &minor)) {
        fprintf(stderr, "eglInitialize fail 0x%x\n", eglGetError());
        return 1;
    }
    printf("EGL vendor: %s\n", eglQueryString(d, EGL_VENDOR));
    printf("EGL version: %s\n", eglQueryString(d, EGL_VERSION));
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(d, attrs, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "no config 0x%x\n", eglGetError()); return 1;
    }
    ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxattrs);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "ctx fail 0x%x\n", eglGetError()); return 1; }
    ps = eglCreatePbufferSurface(d, cfg, pbuf);
    if (ps == EGL_NO_SURFACE) { fprintf(stderr, "pbuf fail 0x%x\n", eglGetError()); return 1; }
    eglMakeCurrent(d, ps, ps, ctx);
    printf("GL renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL version: %s\n", glGetString(GL_VERSION));

    // Render into an FBO-backed texture.
    GLuint fbo, tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete\n"); return 1;
    }
    glViewport(0, 0, w, h);
    glClearColor(0.2f, 0.4f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    printf("Rendered FBO %dx%d (tex %u) OK on %s\n", w, h, tex, node);

    // Export texture as dma-buf via EGL_MESA_image_dma_buf_export
    typedef EGLImageKHR (EGLAPIENTRYP PFN_IMG)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint*);
    typedef EGLBoolean (EGLAPIENTRYP PFN_Q)(EGLDisplay, EGLImageKHR, int*, int*, EGLuint64KHR*);
    typedef EGLBoolean (EGLAPIENTRYP PFN_E)(EGLDisplay, EGLImageKHR, int*, EGLint*, EGLint*);
    PFN_IMG createImg = (PFN_IMG)eglGetProcAddress("eglCreateImageKHR");
    PFN_Q query = (PFN_Q)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    PFN_E export_ = (PFN_E)eglGetProcAddress("eglExportDMABUFImageMESA");
    if (!createImg || !query || !export_) {
        fprintf(stderr, "proc addr missing create=%p q=%p e=%p\n",
                (void*)createImg, (void*)query, (void*)export_);
        return 2;
    }
    EGLImage img = createImg(d, EGL_NO_CONTEXT, EGL_GL_TEXTURE_2D_KHR,
                             (EGLClientBuffer)(intptr_t)tex, NULL);
    if (img == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "eglCreateImageKHR fail 0x%x\n", eglGetError());
        return 2;
    }
    int fourcc = 0, num_planes = 0;
    EGLuint64KHR mods[4] = {0};
    if (query(d, img, &fourcc, &num_planes, mods)) {
        printf("query OK fourcc=0x%x planes=%d mods[0]=0x%lx\n",
               fourcc, num_planes, (unsigned long)mods[0]);
        int fds[4] = {-1,-1,-1,-1};
        EGLint strides[4] = {0}, offsets[4] = {0};
        if (export_(d, img, fds, strides, offsets)) {
            printf("EXPORT OK fd=%d stride=%d offset=%d plane0\n",
                   fds[0], strides[0], offsets[0]);
            return 0;
        } else {
            fprintf(stderr, "eglExportDMABUFImageMESA fail 0x%x\n", eglGetError());
            return 3;
        }
    } else {
        fprintf(stderr, "eglExportDMABUFImageQueryMESA fail 0x%x\n", eglGetError());
        return 3;
    }
}