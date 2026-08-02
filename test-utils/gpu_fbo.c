#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// Render a GPU gradient to an FBO, read back pixels, write to /dev/fb0
static const char *vs =
    "attribute vec2 pos; varying vec2 vpos; void main(){ vpos=pos; gl_Position=vec4(pos,0.0,1.0); }";
static const char *fs =
    "precision mediump float; varying vec2 vpos; void main(){ gl_FragColor=vec4(vpos.x,vpos.y,0.5,1.0); }";

static GLuint compile(GLenum t, const char*s){
    GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,NULL); glCompileShader(sh);
    GLint ok; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){ char l[512]; glGetShaderInfoLog(sh,512,NULL,l); fprintf(stderr,"shader err: %s\n",l); }
    return sh;
}
int main(){
    EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj,min;
    if(!eglInitialize(dpy,&maj,&min)){fprintf(stderr,"egl init fail\n");return 1;}
    EGLint attr[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    EGLConfig cfg; EGLint n;
    if(!eglChooseConfig(dpy,attr,&cfg,1,&n)){fprintf(stderr,"no cfg\n");return 1;}
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_CONTEXT,NULL);
    if(ctx==EGL_NO_CONTEXT){fprintf(stderr,"no ctx\n");return 1;}
    EGLint pa[]={EGL_WIDTH,480,EGL_HEIGHT,320,EGL_NONE};
    EGLSurface sur=eglCreatePbufferSurface(dpy,cfg,pa);
    if(!eglMakeCurrent(dpy,sur,sur,ctx)){fprintf(stderr,"makecur fail\n");return 1;}
    printf("GPU GL: %s\n",glGetString(GL_RENDERER));

    GLuint prog,vbo;
    
    GLuint vs_=compile(GL_VERTEX_SHADER,vs);
    GLuint fs_=compile(GL_FRAGMENT_SHADER,fs);
    prog=glCreateProgram(); glAttachShader(prog,vs_); glAttachShader(prog,fs_); glLinkProgram(prog);
    glUseProgram(prog);
    float verts[]={-1,-1, 1,-1, -1,1, 1,1};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    GLint pos=glGetAttribLocation(prog,"pos");
    glEnableVertexAttribArray(pos); glVertexAttribPointer(pos,2,GL_FLOAT,GL_FALSE,0,0);

    // FBO for readback
    GLuint fbo, tex;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,480,320,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    glViewport(0,0,480,320);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glFinish();

    unsigned char *px=malloc(480*320*3);
    glReadPixels(0,0,480,320,GL_RGB,GL_UNSIGNED_BYTE,px);
    printf("readback: px[0]=%d,%d,%d px[last]=%d,%d,%d\n",
        px[0],px[1],px[2],px[480*320*3-3],px[480*320*3-2],px[480*320*3-1]);

    // convert RGB->RGB565 and write to fb0 (full frame)
    int fb=open("/dev/fb0",O_RDWR);
    unsigned char *rgb565=malloc(480*320*2);
    for(int i=0;i<480*320;i++){
        unsigned r=px[i*3],g=px[i*3+1],b=px[i*3+2];
        rgb565[i*2]=(r&0xf8)|(g>>5);
        rgb565[i*2+1]=((g&0x1c)<<3)|(b>>3);
    }
    write(fb,rgb565,480*320*2); fsync(fb);
    printf("wrote GPU gradient to /dev/fb0\n");
    return 0;
}
