#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
int main(){
    EGLDisplay dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj,min;
    if(!eglInitialize(dpy,&maj,&min)){printf("init fail 0x%x\n",eglGetError());return 1;}
    EGLint cfg_attr[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    EGLConfig cfg; EGLint n=0;
    if(!eglChooseConfig(dpy,cfg_attr,&cfg,1,&n)){printf("no config 0x%x\n",eglGetError());return 1;}
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_CONTEXT,NULL);
    EGLint pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLSurface sur=eglCreatePbufferSurface(dpy,cfg,pa);
    if(!eglMakeCurrent(dpy,sur,sur,ctx)){printf("makecurrent fail 0x%x\n",eglGetError());return 1;}
    printf("GL_RENDERER: %s\n",glGetString(GL_RENDERER));
    printf("GL_VENDOR: %s\n",glGetString(GL_VENDOR));
    return 0;
}
