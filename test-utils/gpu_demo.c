#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

// GPU offscreen -> SPI framebuffer demo.
// Renders an animated scene on the PowerVR (surfaceless), reads back, writes
// to the ili9486 framebuffer. Proves the offscreen GPU path end-to-end.

static const char *vs =
    "attribute vec2 pos; varying vec2 vpos;"
    "void main(){ vpos=pos; gl_Position=vec4(pos,0.0,1.0); }";
static const char *fs =
    "precision mediump float; uniform float t; varying vec2 vpos;"
    "void main(){"
    "  vec2 p=vpos;"
    "  float r=0.5+0.5*sin(p.x*10.0+t);"
    "  float g=0.5+0.5*cos(p.y*10.0+t*1.3);"
    "  float b=0.5+0.5*sin((p.x+p.y)*8.0+t*0.7);"
    "  gl_FragColor=vec4(r,g,b,1.0);"
    "}";
static GLuint compile(GLenum t,const char*s){
    GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,NULL); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){char l[512];glGetShaderInfoLog(sh,512,NULL,l);fprintf(stderr,"shader: %s\n",l);}
    return sh;
}
int main(){
    int W=960,H=640; // render at fb res
    EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj,min; eglInitialize(dpy,&maj,&min);
    EGLint a[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    EGLConfig cfg; EGLint n;
    eglChooseConfig(dpy,a,&cfg,1,&n);
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_CONTEXT,NULL);
    EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLSurface sur=eglCreatePbufferSurface(dpy,cfg,pa);
    eglMakeCurrent(dpy,sur,sur,ctx);
    printf("GPU: %s\n",glGetString(GL_RENDERER));

    GLuint prog,vbo;
    GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
    prog=glCreateProgram(); glAttachShader(prog,v); glAttachShader(prog,f); glLinkProgram(prog);
    glUseProgram(prog);
    float verts[]={-1,-1, 1,-1, -1,1, 1,1};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    GLint pos=glGetAttribLocation(prog,"pos");
    glEnableVertexAttribArray(pos); glVertexAttribPointer(pos,2,GL_FLOAT,GL_FALSE,0,0);
    GLint tu=glGetUniformLocation(prog,"t");

    GLuint fbo,tex;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,W,H,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    glViewport(0,0,W,H);

    int fb=open("/dev/fb0",O_RDWR);
    if(fb<0){perror("open fb0");return 1;}
    unsigned char *px=malloc(W*H*3), *rgb565=malloc(W*H*2);
    struct timespec ts;
    printf("Rendering GPU frames to SPI display (Ctrl-C to stop)\n");
    for(float t=0;;t+=0.1){
        clock_gettime(CLOCK_MONOTONIC,&ts); long ms=(ts.tv_sec*1000+ts.tv_nsec/1000000)%1000;
        glUniform1f(tu,t);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        glFinish();
        glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px);
        for(int i=0;i<W*H;i++){
            unsigned r=px[i*3],g=px[i*3+1],b=px[i*3+2];
            rgb565[i*2]=(r&0xf8)|(g>>5);
            rgb565[i*2+1]=((g&0x1c)<<3)|(b>>3);
        }
        lseek(fb,0,SEEK_SET); write(fb,rgb565,W*H*2); fsync(fb);
        printf("\rfps=%ld  ",ms?0:0); fflush(stdout);
    }
    return 0;
}
