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
#include <sys/stat.h>
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

static int out_w = 480, out_h = 320;   /* GPU render size */
static int fb_w = 960, fb_h = 640;     /* XRGB8888 fb size (proven scale=2 path) */
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
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(sh, 512, 0, log);
		fprintf(stderr, "shader compile FAILED: %s\n", log);
	}
	return sh;
}

static GLuint program, u_ty, u_tu, u_tv, u_half, vbo, attr_pos;
static GLuint tex_y, tex_u, tex_v;
static EGLDisplay egl_dpy;
static EGLSurface egl_su;

static int setup_gpu(void)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint a, b;
	if (!eglInitialize(dpy, &a, &b)) {
		fprintf(stderr, "EGL init failed (set LD_LIBRARY_PATH=/usr/local/lib)\n");
		return -1;
	}
	egl_dpy = dpy;
	EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			   EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
			   EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	EGLConfig cfg;
	EGLint n;
	eglChooseConfig(dpy, attrs, &cfg, 1, &n);
	/* CRITICAL: without EGL_CONTEXT_CLIENT_VERSION=2 the vendor EGL
	 * silently creates a GLES 1.1 context (no shaders at all). */
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
	/*
	 * Render to an out_w x out_h (480x320) pbuffer's DEFAULT framebuffer
	 * and read back RGBA from there: verified working on this PowerVR
	 * with an ES3.2 context (FBO-attached readback of shader output is
	 * broken - returns black/garbage). 480x320 = 1/4 the readback+copy
	 * traffic of 960x640, and the fb at 480x320 goes through the scaled
	 * path as identity (byte-correct RGB565 to the panel).
	 */
	EGLint pa[] = { EGL_WIDTH, out_w, EGL_HEIGHT, out_h, EGL_NONE };
	EGLSurface su = eglCreatePbufferSurface(dpy, cfg, pa);
	egl_su = su;
	eglMakeCurrent(dpy, su, su, ctx);
	fprintf(stderr, "GPU: %s\n", glGetString(GL_RENDERER));

	GLuint vv = comp(GL_VERTEX_SHADER, VS);
	GLuint ff = comp(GL_FRAGMENT_SHADER, FS);
	program = glCreateProgram();
	glAttachShader(program, vv);
	glAttachShader(program, ff);
	glLinkProgram(program);
	GLint plink = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &plink);
	if (!plink) {
		char log[512];
		glGetProgramInfoLog(program, 512, 0, log);
		fprintf(stderr, "program link FAILED: %s\n", log);
		return -1;
	}
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

	/* 3 luminance textures for YV12 planes.
	 * Y uses LINEAR; U/V use NEAREST so chroma doesn't bleed across
	 * edges when upscaled (the 4:2:0 "halo" artifact). */
	for (int i = 0; i < 3; i++) {
		GLuint t;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				i == 0 ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
				i == 0 ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (i == 0) {
			tex_y = t;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W, VID_H,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_ty, 0);
		} else if (i == 1) {
			tex_u = t;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_tu, 1);
		} else {
			tex_v = t;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, VID_W / 2, VID_H / 2,
				     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			glUniform1i(u_tv, 2);
		}
		glActiveTexture(GL_TEXTURE0 + i);
	}

	glViewport(0, 0, out_w, out_h);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
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

	/* find the mode matching out_w x out_h (480x320 = panel native, the
	 * scaled path treats it as identity and produces byte-correct
	 * high-byte-first RGB565 for the panel) */
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

	/* ---- raw YV12 mode: skip the VPU entirely (avoids the cedar VPU /
	 * PowerVR ion allocation conflict that stalls the decode when both
	 * run concurrently) ---- */
	int raw_mode = strstr(argv[1], ".yv12") != NULL;
	int ifd = -1;
	pid_t pid = 0;

	/* ---- decode in background FIRST (before GPU/EGL init, which can
	 * disturb the cedar VPU driver's ion/dma-buf allocations) ---- */
	int is_url = !raw_mode && (strncmp(argv[1], "http://", 7) == 0 ||
		     strncmp(argv[1], "https://", 8) == 0);
	int is_mkv = !raw_mode && strstr(argv[1], ".mkv") != NULL;
	char src_arg[640], rawloc[640];
	char *raw = "/tmp/gpu_player.yv12";
	if (is_url)
		snprintf(src_arg, sizeof(src_arg), "location=%s", argv[1]);
	else
		snprintf(src_arg, sizeof(src_arg), "location=%s", argv[1]);
	snprintf(rawloc, sizeof(rawloc), "location=%s", raw);
	if (!raw_mode)
		unlink(raw);
	if (!raw_mode) {
		fprintf(stderr, "decoding with VPU (%s)...\n", is_url ? "URL" : "file");
		pid = fork();
		if (pid == 0) {
			int errfd = open("/tmp/gstchild.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (errfd >= 0)
				dup2(errfd, 2);
			if (is_url)
				execlp("gst-launch-1.0", "gst-launch-1.0", "-q",
				       "curlhttpsrc", src_arg,
				       "!", is_mkv ? "matroskademux" : "qtdemux",
				       "!", "h264parse",
				       "!", "omxh264dec",
				       "!", "filesink", rawloc,
				       NULL);
			else
				execlp("gst-launch-1.0", "gst-launch-1.0", "-q",
				       "filesrc", src_arg,
				       "!", is_mkv ? "matroskademux" : "qtdemux",
				       "!", "h264parse",
				       "!", "omxh264dec",
				       "!", "filesink", rawloc,
				       NULL);
			perror("execlp gst-launch-1.0");
			_exit(1);
		}
	} else {
		fprintf(stderr, "raw YV12 mode, reading %s\n", argv[1]);
		ifd = open(argv[1], O_RDONLY);
		if (ifd < 0) { perror(argv[1]); return 1; }
	}

	/* now init the GPU and DRM display (after the decoder is running) */
	if (setup_gpu()) return 1;
	if (setup_drm()) return 1;

	/* wait for ~1s of frames (30 frames) to buffer; if the decoder died
	 * before buffering, relaunch it (cedar VPU is flaky after use) */
	long buffered = 0;
	if (!raw_mode) {
	for (int attempt = 0; attempt < 3 && ifd < 0; attempt++) {
		if (attempt > 0) {
			fprintf(stderr, "decode attempt %d\n", attempt + 1);
			unlink(raw);
			usleep(500000);
			pid = fork();
			if (pid == 0) {
				int errfd = open("/tmp/gstchild.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (errfd >= 0)
					dup2(errfd, 2);
				execlp("gst-launch-1.0", "gst-launch-1.0", "-q",
				       "filesrc", src_arg,
				       "!", is_mkv ? "matroskademux" : "qtdemux",
				       "!", "h264parse",
				       "!", "omxh264dec",
				       "!", "filesink", rawloc,
				       NULL);
				perror("execlp gst-launch-1.0");
				_exit(1);
			}
		}
		for (int w = 0; w < 300 && ifd < 0; w++) {
			if (kill(pid, 0) != 0)
				break;          /* decoder died, retry */
			usleep(100000);
			struct stat st;
			if (stat(raw, &st) == 0) {
				buffered = st.st_size / FRAME_SIZE;
				if (w % 5 == 0)
					fprintf(stderr, "buffered: %ld frames\n", buffered);
				if (buffered >= 30) {
					ifd = open(raw, O_RDONLY);
					fprintf(stderr, "buffered %ld frames, starting\n", buffered);
				}
			} else {
				fprintf(stderr, "raw file not created yet\n");
			}
		}
	}
	}
	if (ifd < 0) {
		fprintf(stderr, "no decode data arrived\n");
		return 1;
	}

	unsigned char *frame = malloc(FRAME_SIZE);
	unsigned char *y = frame;
	unsigned char *v = frame + VID_WSZ;              /* YV12: V first */
	unsigned char *u = frame + VID_WSZ + VID_WSZ / 4;

	fprintf(stderr, "GPU player running %dx%d (Ctrl-C to quit)\n", out_w, out_h);
	long frames = 0;
	double t_last = 0;

		size_t got = 0;
	while (!quit) {
		/* read one frame; if the file is still growing (progressive
		 * playback), wait for more data instead of quitting */
		struct timespec t0r;
		clock_gettime(CLOCK_MONOTONIC, &t0r);
		got = 0;
		while (got < FRAME_SIZE) {
			ssize_t r = read(ifd, frame + got, FRAME_SIZE - got);
			if (r <= 0) {
				if (raw_mode)
					break;      /* EOF: loop below */
				/* real EOF only when the decoder has fully exited */
				int wst;
				pid_t wr = waitpid(pid, &wst, WNOHANG);
				if (wr == 0) {
					usleep(5000);       /* decoder still alive, keep waiting */
					continue;
				}
				quit = 1;
				break;
			}
			got += r;
		}
		if (got < FRAME_SIZE) {
			/* loop the raw file so playback is continuous */
			if (raw_mode) {
				lseek(ifd, 0, SEEK_SET);
				got = 0;
				continue;
			}
			break;
		}
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
		glBindTexture(GL_TEXTURE_2D, tex_y);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W, VID_H,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, y);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex_u);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W / 2, VID_H / 2,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, u);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, tex_v);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VID_W / 2, VID_H / 2,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, v);

		/* render to the pbuffer default framebuffer, read RGBA (verified
		 * working with ES3.2), write to the out_w x out_h fb as
		 * XRGB8888 (memory order B,G,R,X) */
		glUseProgram(program);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glFinish();
		clock_gettime(CLOCK_MONOTONIC, &t1t);

		static unsigned char *stage;
		if (!stage)
			stage = malloc(out_w * out_h * 4);
		glReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE, stage);
		glFinish();
		for (int i = 0; i < out_w * out_h; i++) {
			fb_map[i * 4]     = stage[i * 4 + 2]; /* B <- R */
			fb_map[i * 4 + 1] = stage[i * 4 + 1]; /* G <- G */
			fb_map[i * 4 + 2] = stage[i * 4];     /* R <- B */
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
	if (pid > 0) {
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
	}
	unlink(raw);
	/* keep the last frame on screen so the display doesn't go gray when
	 * the fb is freed on exit */
	fprintf(stderr, "end of video, holding last frame (Ctrl-C to exit)\n");
	while (!quit)
		usleep(500000);
	return 0;
}
