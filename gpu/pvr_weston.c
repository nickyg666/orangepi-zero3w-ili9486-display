// pvr_weston.c - fullscreen GPU-accelerated Wayland client.
// Renders GLES3 frames on the PowerVR into GPU dmabuf buffers, imports them into
// Weston via zwp_linux_dmabuf_v1, and presents on DP-1 at the requested size.
// Requires weston-drm running (root, world-writable socket wl-drm).
// Build:
//   gcc -O2 -o pvr_weston pvr_weston.c pvr_offscreen.c linux-dmabuf-unstable-v1-protocol.c \
//       -I. -I/usr/include -L/usr/local/lib -lgbm -lEGL -lGLESv2 \
//       -lwayland-client -Wl,-rpath,/usr/local/lib
// Run:
//   XDG_RUNTIME_DIR=/run/weston-rt WAYLAND_DISPLAY=wl-drm \
//   LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri ./pvr_weston 1920 1080
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <wayland-client.h>
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "pvr_offscreen.h"

struct app {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct zwp_linux_dmabuf_v1 *dmabuf;
    struct wl_surface *surface;
    int have_xrgb8888;
    /* per-frame state */
    struct wl_buffer *pending_wb;   /* buffer received via 'created' */
    int params_created;             /* 1 created, -1 failed */
    int frame_done;                 /* frame callback fired */
    PvrCtx *ctx;
};

static void frame_callback(void *data, struct wl_callback *cb, uint32_t time) {
    struct app *a = data;
    a->frame_done = 1;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_listener = { frame_callback };

static void params_created(void *data, struct zwp_linux_buffer_params_v1 *p,
                           struct wl_buffer *buffer) {
    struct app *a = data;
    a->pending_wb = buffer;
    a->params_created = 1;
}
static void params_failed(void *data, struct zwp_linux_buffer_params_v1 *p) {
    struct app *a = data;
    a->params_created = -1;
}
static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    params_created, params_failed
};

static void dmabuf_format(void *d, struct zwp_linux_dmabuf_v1 *z, uint32_t format) {
    if (format == 0x20bc5458) ((struct app*)d)->have_xrgb8888 = 1;
}
static void dmabuf_modifier(void *d, struct zwp_linux_dmabuf_v1 *z, uint32_t format,
                            uint32_t mod_hi, uint32_t mod_lo) {}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    dmabuf_format, dmabuf_modifier
};

static void registry_global(void *d, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version) {
    struct app *a = d;
    if (!strcmp(iface, wl_compositor_interface.name))
        a->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name)) {
        a->dmabuf = wl_registry_bind(reg, name, &zwp_linux_dmabuf_v1_interface, 1);
        zwp_linux_dmabuf_v1_add_listener(a->dmabuf, &dmabuf_listener, a);
    }
}
static void registry_global_remove(void *d, struct wl_registry *reg, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove
};

static double now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec/1e9; }

static const char *VS =
  "attribute vec2 aPos;\n"
  "varying vec4 vColor;\n"
  "uniform float uT;\n"
  "void main(){\n"
  "  gl_Position=vec4(aPos,0.0,1.0);\n"
  "  float x=0.5*(aPos.x+1.0); float y=0.5*(aPos.y+1.0);\n"
  "  float k=sin((x*20.0+uT))*cos((y*20.0+uT*0.7));\n"
  "  vColor=vec4(x,y,0.5+0.5*k,1.0);\n"
  "}";
static const char *FS =
  "precision mediump float;\n"
  "varying vec4 vColor;\n"
  "void main(){ gl_FragColor=vColor; }";
static const float verts[] = { -1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1 };

int main(int argc,char**argv){
    int w = argc>1 ? atoi(argv[1]) : 1920;
    int h = argc>2 ? atoi(argv[2]) : 1080;
    int frames = argc>3 ? atoi(argv[3]) : 600;

    struct app app;
    memset(&app, 0, sizeof app);

    app.display = wl_display_connect(NULL);
    if (!app.display) { fprintf(stderr, "wl_display_connect failed\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(app.display);
    wl_registry_add_listener(reg, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);
    if (!app.compositor) { fprintf(stderr, "no compositor\n"); return 1; }
    if (!app.dmabuf) { fprintf(stderr, "compositor lacks linux-dmabuf\n"); return 1; }
    fprintf(stderr,"STAGE1: compositor=ok dmabuf=ok XRGB8888=%d\n", app.have_xrgb8888);

    app.surface = wl_compositor_create_surface(app.compositor);
    wl_surface_set_buffer_scale(app.surface, 1);

    app.ctx = pvr_create("/dev/dri/renderD129", 2);
    if (!app.ctx) { fprintf(stderr, "pvr create fail\n"); return 1; }
    printf("GPU renderer: %s\n", pvr_renderer_string(app.ctx));

    char log[512]; GLint ok=0;
    GLuint vs=glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs,1,&VS,NULL); glCompileShader(vs);
    GLuint fs=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs,1,&FS,NULL); glCompileShader(fs);
    GLuint prog=glCreateProgram(); glAttachShader(prog,vs); glAttachShader(prog,fs); glLinkProgram(prog);
    glGetProgramiv(prog,GL_LINK_STATUS,&ok);
    if(!ok){ glGetProgramInfoLog(prog,sizeof log,NULL,log); fprintf(stderr,"link: %s\n",log); return 1; }
    GLint upos=glGetAttribLocation(prog,"aPos");
    GLint utT=glGetUniformLocation(prog,"uT");
    glUseProgram(prog);
    glEnableVertexAttribArray(upos);
    GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof verts,verts,GL_STATIC_DRAW);
    glVertexAttribPointer(upos,2,GL_FLOAT,GL_FALSE,0,0);

    double t0 = now();
    int delivered = 0;
    for (int i=0; i<frames; ) {
        /* render on GPU */
        PvrBuffer b = pvr_begin_frame(app.ctx, w, h);
        if (b.fd < 0) { fprintf(stderr,"frame %d: no buffer\n",i); break; }
        glUniform1f(utT, (float)i/10.0f);
        glDrawArrays(GL_TRIANGLES,0,6);
        glFinish();

        /* request dmabuf buffer via params; wait for 'created' event */
        struct zwp_linux_buffer_params_v1 *params =
            zwp_linux_dmabuf_v1_create_params(app.dmabuf);
        zwp_linux_buffer_params_v1_add(params, b.fd, 0, 0, b.stride, 0, 0);
        zwp_linux_buffer_params_v1_add_listener(params, &params_listener, &app);
        zwp_linux_buffer_params_v1_create(params, w, h, b.fourcc, 0);

        app.params_created = 0;
        app.pending_wb = NULL;
        wl_display_flush(app.display);
        int fd = wl_display_get_fd(app.display);
        int guard = 2000;
        while (app.pending_wb == NULL && guard-- > 0) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
            int r = poll(&pfd, 1, 20);
            if (r > 0) wl_display_dispatch(app.display);
            else if (r == 0) wl_display_dispatch_pending(app.display);
            if (wl_display_get_error(app.display)) break;
        }
        if (!app.pending_wb) {
            fprintf(stderr,"frame %d: params_created=%d no buffer, err=%s\n",
                    i, app.params_created, strerror(errno));
            break;
        }

        /* arm frame callback, attach, commit */
        app.frame_done = 0;
        struct wl_callback *fcb = wl_surface_frame(app.surface);
        wl_callback_add_listener(fcb, &frame_listener, &app);
        wl_surface_attach(app.surface, app.pending_wb, 0, 0);
        wl_surface_damage(app.surface, 0, 0, w, h);
        wl_surface_commit(app.surface);
        wl_display_flush(app.display);

        i++;
        delivered++;
        close(b.fd);
    }
    double t1 = now();
    printf("Delivered %d/%d frames at %dx%d in %.3fs = %.1f fps\n",
           delivered, frames, w, h, t1-t0, delivered/(t1-t0));

    wl_surface_destroy(app.surface);
    pvr_destroy(app.ctx);
    wl_display_disconnect(app.display);
    return 0;
}