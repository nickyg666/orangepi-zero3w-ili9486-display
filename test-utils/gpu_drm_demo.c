// GPU offscreen -> DRM card1 (SPI display) via atomic flip.
// Renders on PowerVR (surfaceless EGL), copies to a card1 dumb fb, flips.
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static const char *vs="attribute vec2 pos; varying vec2 vp; void main(){vp=pos;gl_Position=vec4(pos,0.,1.);}";
static const char *fs="precision mediump float;uniform float t;varying vec2 vp;"
 "void main(){float r=.5+.5*sin(vp.x*10.+t);float g=.5+.5*cos(vp.y*10.+t*1.3);"
 "float b=.5+.5*sin((vp.x+vp.y)*8.+t*.7);gl_FragColor=vec4(r,g,b,1.);}";
static GLuint c(GLenum t,const char*s){GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,0);glCompileShader(sh);return sh;}
int main(){
    int W=960,H=640;
    EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint a_,b_;eglInitialize(dpy,&a_,&b_);
    EGLint aa[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    EGLConfig cf;EGLint n;eglChooseConfig(dpy,aa,&cf,1,&n);
    EGLContext ctx=eglCreateContext(dpy,cf,EGL_NO_CONTEXT,0);
    EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLSurface su=eglCreatePbufferSurface(dpy,cf,pa);eglMakeCurrent(dpy,su,su,ctx);
    printf("GPU: %s\n",glGetString(GL_RENDERER));
    GLuint pr;
    GLuint vv=c(GL_VERTEX_SHADER,vs),ff=c(GL_FRAGMENT_SHADER,fs);
    pr=glCreateProgram();glAttachShader(pr,vv);glAttachShader(pr,ff);glLinkProgram(pr);glUseProgram(pr);
    float vs2[]={-1,-1,1,-1,-1,1,1,1};GLuint vb;glGenBuffers(1,&vb);glBindBuffer(GL_ARRAY_BUFFER,vb);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vs2),vs2,GL_STATIC_DRAW);
    GLint p=glGetAttribLocation(pr,"pos");glEnableVertexAttribArray(p);glVertexAttribPointer(p,2,GL_FLOAT,0,0,0);
    GLint tu=glGetUniformLocation(pr,"t");
    GLuint fbo,tx;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&tx);glBindTexture(GL_TEXTURE_2D,tx);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,W,H,0,GL_RGB,GL_UNSIGNED_BYTE,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tx,0);
    glViewport(0,0,W,H);

    // DRM on card1
    int fd=open("/dev/dri/card1",O_RDWR);
    if(fd<0){perror("card1");return 1;}
    drmModeRes*res=drmModeGetResources(fd);
    // find connected connector + crtc
    uint32_t conn=0,crtc=0;
    for(int i=0;i<res->count_connectors;i++){
        drmModeConnector*c=drmModeGetConnector(fd,res->connectors[i]);
        if(c->connection==DRM_MODE_CONNECTED&&c->count_modes){conn=c->connector_id;
            // find crtc
            for(int j=0;j<res->count_crtcs;j++){crtc=res->crtcs[j];break;} break;}
        drmModeFreeConnector(c);
    }
    printf("conn=%u crtc=%u\n",conn,crtc);
    if(!conn||!crtc){printf("no output\n");return 1;}
    drmSetMaster(fd);
    // dumb fb
    struct drm_mode_create_dumb cdb={0};cdb.width=W;cdb.height=H;cdb.bpp=32;
    ioctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&cdb);
    struct drm_mode_map_dumb md={0};md.handle=cdb.handle;ioctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&md);
    unsigned char*map=mmap(0,cdb.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,md.offset);
    uint32_t fb=0;drmModeAddFB(fd,W,H,24,32,W*4,cdb.handle,&fb);
    drmModeConnector*c2=drmModeGetConnector(fd,conn);
    drmModeSetCrtc(fd,crtc,fb,0,0,&conn,1,&c2->modes[0]);drmModeFreeConnector(c2);
    unsigned char*px=malloc(W*H*3);
    printf("GPU->DRM flip loop (Ctrl-C to stop)\n");
    for(float t=0;;t+=0.1){
        glUniform1f(tu,t);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();
        glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px);
        for(int i=0;i<W*H;i++){map[i*4]=px[i*3+2];map[i*4+1]=px[i*3+1];map[i*4+2]=px[i*3];map[i*4+3]=0xff;}
        // page flip via legacy setcrtc (or atomic)
        drmModeSetCrtc(fd,crtc,fb,0,0,&conn,1,0);
    }
    return 0;
}
