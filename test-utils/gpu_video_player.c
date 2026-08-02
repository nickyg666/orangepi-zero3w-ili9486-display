/*
 * gpu_video_player - VPU + PowerVR GPU video player for the ILI9486 SPI display
 *
 * Pipeline:
 *   gst-launch (child): filesrc -> qtdemux -> h264parse -> omxh264dec (VPU,
 *   hardware H.264 decode) -> raw YV12 frames piped to us on stdin
 *   this app: YV12 -> PowerVR GLES2 (3 luminance textures, fragment shader
 *   does YUV->RGB + scaling) -> FBO at panel res (RGB565 render target) ->
 *   glReadPixels straight into the card1 dumb fb -> DRM flip -> SPI panel.
 *
 * The GPU does every per-pixel operation (colorspace conversion, scaling).
 * The CPU only memcpy's decoded YV12 in and reads packed RGB565 out
 * (glReadPixels is a pure copy; no CPU color math).  The 16-bit SPI word
 * mode (bpw=16) transmits each u16 MSB-first, so a native little-endian
 * RGB565 framebuffer arrives at the panel in the correct wire order with
 * zero byte shuffling.
 *
 * Usage: stop X first (systemctl stop gdm3), then:
 *   gpu_video_player <video.mp4> [out_w out_h]
 *
 * Requires: MESA_LOADER_DRIVER_OVERRIDE=pvr EGL_PLATFORM=surfaceless
 *           LD_LIBRARY_PATH=/usr/local/lib
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define VID_W 1280
#define VID_H 736
#define VID_WSZ (VID_W * VID_H)
#define FRAME_SIZE (VID_WSZ + VID_WSZ / 2)   /* YV12: Y + V + U at half res */

static volatile int quit = 0;
static void onint(int x) { (void)x; quit = 1; }

static int out_w = 480, out_h = 320;   /* panel size (scaled down from fb) */
static int fb_w = 960, fb_h = 640;     /* XRGB8888 fb size (scale=2 path) */
static unsigned char *fb_map;
static int drm_fd = -1;
static uint32_t drm_conn, drm_crtc, drm_fb;
static drmModeModeInfo drm_mode;

static const char *VS =
	"attribute vec2 pos;"
	"varying vec2 vp;"
	"void main(){vp=pos*.5+.5;gl_Position=vec4(pos,0.,1.);}";
static const char *FS =
	"precision mediump float;"
	"uniform sampler2D ty,tu,tv;"
	"uniform vec2 halfuv;"
	"varying vec2 vp;"
	"void main(){"
	"  vec2 uv=vp;"
	"  float y=texture2D(ty,uv).r;"
	"  float u=texture2D(tu,uv*halfuv).r-.5;"
	"  float v=texture2D(tv,uv*halfuv).r-.5;"
	"  float r=y+1.402*v;"
	"  float g=y-.344*u-.714*v;"
	"  float b=y+1.772*u;"
	"  gl_FragColor=vec4(r,g,b,1.);"
	"}";

static GLuint comp(GLenum t, const char *s)
{
	GLuint sh = glCreateShader(t);
	glShaderSource(sh, 1, &s, 0);
	glCompileShader(sh);
	return sh;
}

static GLuint program, u_ty, u_tu, u_tv, u_half, fbo, fbo_tex, vbo, attr_pos;

static int setup_gpu(void)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint a, b;
	if (!eglInitialize(dpy, &a, &b)) {
		fprintf(stderr, "EGL init failed (set LD_LIBRARY_PATH=/usr/local/lib)\n");
		return -1;
	}
	EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			   EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
	EGLConfig cfg;
	EGLint n;
	eglChooseConfig(dpy, attrs, &cfg, 1, &n);
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
	EGLint pa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	EGLSurface su = eglCreatePbufferSurface(dpy, cfg, pa);
	eglMakeCurrent(dpy, su, su, ctx);
	fprintf(stderr, "GPU: %s\n", glGetString(GL_RENDERER));

	GLuint vv = comp(GL_VERTEX_SHADER, VS);
	GLuint ff = comp(GL_FRAGMENT_SHADER, FS);
	program = glCreateProgram();
	glAttachShader(program, vv);
	glAttachShader(program, ff);
	glLinkProgram(program);
	glUseProgram(program);

	static const float quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	attr_pos = glGetAttribLocation(program, "pos");
	glEnableVertexAttribArray(attr_pos);
	glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 0, 0);

	u_ty = glGetUniformLocation(program, "ty");
	u_tu = glGetUniformLocation(program, "tu");
	u_tv = glGetUniformLocation(program, "tv");
	u_half = glGetUniformLocation(program, "halfuv");
	glUniform2f(u_half, 0.5, 0.5);

	/* 3 luminance textures for YV12 planes */
	for (int i = 0; i < 3; i++) {
		GLuint t;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (i == 0) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W, VID_H,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_ty, 0);
		} else if (i == 1) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_tu, 1);
		} else {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_tv, 2);
		}
		glActiveTexture(GL_TEXTURE0 + i);
	}

	/* FBO at panel resolution with native RGB565 render target */
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenTextures(1, &fbo_tex);
	glBindTexture(GL_TEXTURE_2D, fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, out_w, out_h, 0,
		     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, fbo_tex, 0);
	glViewport(0, 0, out_w, out_h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	return 0;
}

static int setup_drm(void)
{
	drm_fd = open("/dev/dri/card1", O_RDWR);
	if (drm_fd < 0) { perror("card1"); return -1; }
	if (drmSetMaster(drm_fd)) {
		perror("drmSetMaster (stop X first: systemctl stop gdm3)");
		return -1;
	}
	drmModeRes *res = drmModeGetResources(drm_fd);
	drm_conn = 0;
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes) {
			drm_conn = c->connector_id;
			drmModeFreeConnector(c);
			break;
		}
		drmModeFreeConnector(c);
	}
	if (!drm_conn) { fprintf(stderr, "no connected output\n"); return -1; }
	drm_crtc = res->crtcs[0];

	struct drm_mode_create_dumb cdb = {0};
	cdb.width = out_w; cdb.height = out_h; cdb.bpp = 32;
	ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cdb);
	struct drm_mode_map_dumb md = {0};
	md.handle = cdb.handle;
	ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
	fb_map = mmap(0, cdb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      drm_fd, md.offset);
	if (fb_map == MAP_FAILED) { perror("mmap"); return -1; }

	/* find the mode matching out_w x out_h (panel res, scale=1) */
	drmModeConnector *c = drmModeGetConnector(drm_fd, drm_conn);
	drmModeModeInfo *mode = NULL;
	for (int i = 0; i < c->count_modes; i++)
		if (c->modes[i].hdisplay == out_w && c->modes[i].vdisplay == out_h) {
			mode = &c->modes[i];
			break;
		}
	if (!mode) {
		fprintf(stderr, "no %dx%d mode\n", out_w, out_h);
		return -1;
	}
	drm_mode = *mode;
	/* XRGB8888 fb: the kernel's scaled path converts it to high-byte-first
	 * RGB565 for the panel (byte-correct regardless of SPI word order) */
	drmModeAddFB(drm_fd, out_w, out_h, 24, 32, out_w * 4, cdb.handle, &drm_fb);
	drmModeSetCrtc(drm_fd, drm_crtc, drm_fb, 0, 0, &drm_conn, 1, mode);
	drmModeFreeConnector(c);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <video> [out_w out_h]\n", argv[0]);
		return 1;
	}
	if (argc > 2) out_w = atoi(argv[2]);
	if (argc > 3) out_h = atoi(argv[3]);
	signal(SIGINT, onint);

	if (setup_gpu()) return 1;
	if (setup_drm()) return 1;

	/* ---- pre-decode: VPU -> raw YV12 file (no pipe backpressure) ---- */
	char location[512];
	snprintf(location, sizeof(location), "location=%s", argv[1]);
	char rawloc[512];
	snprintf(rawloc, sizeof(rawloc), "location=%s", "/tmp/gpu_player.yv12");
	char *raw = "/tmp/gpu_player.yv12";
	unlink(raw);
	fprintf(stderr, "pre-decoding with VPU...\n");
	pid_t pid = fork();
	if (pid == 0) {
		int errfd = open("/tmp/gstchild.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (errfd >= 0)
			dup2(errfd, 2);
		execlp("gst-launch-1.0", "gst-launch-1.0", "-q",
		       "filesrc", location,
		       "!", "qtdemux",
		       "!", "h264parse",
		       "!", "omxh264dec",
		       "!", "filesink", rawloc,
		       NULL);
		perror("execlp gst-launch-1.0");
		_exit(1);
	}
	int status;
	waitpid(pid, &status, 0);
	fprintf(stderr, "decode done, status %d\n", status);

	int ifd = open(raw, O_RDONLY);
	if (ifd < 0) { perror("raw"); return 1; }

	unsigned char *frame = malloc(FRAME_SIZE);
	unsigned char *y = frame;
	unsigned char *v = frame + VID_WSZ;              /* YV12: V first */
	unsigned char *u = frame + VID_WSZ + VID_WSZ / 4;

	fprintf(stderr, "GPU player running %dx%d (Ctrl-C to quit)\n", out_w, out_h);
	long frames = 0;
	double t_last = 0;
	GLuint yt = 0, ut = 0, vt = 0;
	/* capture texture ids: we created them in order 0=Y,1=U,2=V */
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &yt);
	glBindTexture(GL_TEXTURE_2D, yt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W, VID_H,
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	glActiveTexture(GL_TEXTURE1);
	glGenTextures(1, &ut);
	glBindTexture(GL_TEXTURE_2D, ut);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	glActiveTexture(GL_TEXTURE2);
	glGenTextures(1, &vt);
	glBindTexture(GL_TEXTURE_2D, vt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

	size_t got = 0;
	while (!quit) {
		/* read one frame */
		struct timespec t0r;
		clock_gettime(CLOCK_MONOTONIC, &t0r);
		got = 0;
		while (got < FRAME_SIZE) {
			ssize_t r = read(ifd, frame + got, FRAME_SIZE - got);
			if (r <= 0) { quit = 1; break; }
			got += r;
		}
		if (got < FRAME_SIZE) break;
		if (frames < 3) {
			struct timespec t1r;
			clock_gettime(CLOCK_MONOTONIC, &t1r);
			fprintf(stderr, "read %.1fms\n",
				(t1r.tv_sec - t0r.tv_sec) * 1000.0 +
				(t1r.tv_nsec - t0r.tv_nsec) / 1e6);
		}

		/* upload YV12 planes to GPU */
		struct timespec t0t, t1t;
		clock_gettime(CLOCK_MONOTONIC, &t0t);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, yt);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W, VID_H,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, y);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, ut);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W / 2, VID_H / 2,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, u);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, vt);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W / 2, VID_H / 2,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, v);

		/* render frame to RGB565 FBO */
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glUseProgram(program);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glFinish();
		clock_gettime(CLOCK_MONOTONIC, &t1t);

		/* read RGB565 from GPU, expand to XRGB8888 for the kernel's
		 * byte-correct scaled path (panel needs high-byte-first) */
		static unsigned char *stage;
		if (!stage)
			stage = malloc(out_w * out_h * 2);
		glReadPixels(0, 0, out_w, out_h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
			     stage);
		glFinish();
		for (int i = 0; i < out_w * out_h; i++) {
			unsigned short p = stage[i * 2] | (stage[i * 2 + 1] << 8);
			unsigned r = (p >> 11) & 0x1f;
			unsigned g = (p >> 5) & 0x3f;
			unsigned b = p & 0x1f;
			fb_map[i * 4]     = (b << 3) | (b >> 2);
			fb_map[i * 4 + 1] = (g << 2) | (g >> 4);
			fb_map[i * 4 + 2] = (r << 3) | (r >> 2);
			fb_map[i * 4 + 3] = 0xff;
		}
		struct timespec t2;
		clock_gettime(CLOCK_MONOTONIC, &t2);
		(void)t2;

		/* flip */
		struct timespec t3;
		clock_gettime(CLOCK_MONOTONIC, &t3);
		int scr = drmModeSetCrtc(drm_fd, drm_crtc, drm_fb, 0, 0, &drm_conn, 1,
				       &drm_mode);
		struct timespec t4;
		clock_gettime(CLOCK_MONOTONIC, &t4);
		if (scr)
			fprintf(stderr, "SetCrtc error %d\n", scr);
		if (frames < 3)
			fprintf(stderr, "loop total %.1fms\n",
				(t4.tv_sec - t0r.tv_sec) * 1000.0 +
				(t4.tv_nsec - t0r.tv_nsec) / 1e6);

		frames++;
		if (frames <= 5)
			fprintf(stderr, "frame %ld: upload+render %.1fms, flip %.1fms\n",
				frames,
				(t1t.tv_sec - t0t.tv_sec) * 1000.0 +
				(t1t.tv_nsec - t0t.tv_nsec) / 1e6,
				(t4.tv_sec - t3.tv_sec) * 1000.0 +
				(t4.tv_nsec - t3.tv_nsec) / 1e6);
		if (frames % 30 == 0) {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			double now = ts.tv_sec + ts.tv_nsec / 1e9;
			fprintf(stderr, "%ld frames, %.1f fps\n", frames,
				(t_last > 0) ? 30.0 / (now - t_last) : 0);
			t_last = now;
		}
	}
	fprintf(stderr, "\nplayed %ld frames\n", frames);
	unlink(raw);
	return 0;
}
