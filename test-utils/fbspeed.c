#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
static long long ns(void){struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;}
int main(){
    int fd=open("/dev/fb1",O_RDWR);
    if(fd<0){perror("open");return 1;}
    struct fb_fix_screeninfo fix; struct fb_var_screeninfo var;
    ioctl(fd,FBIOGET_FSCREENINFO,&fix); ioctl(fd,FBIOGET_VSCREENINFO,&var);
    printf("fb1: %dx%d bpp=%d smem=%u pitch=%u\n",var.xres,var.yres,var.bits_per_pixel,fix.smem_len,fix.line_length);
    void *buf=malloc(fix.smem_len); memset(buf,0x0F,fix.smem_len);
    /* warm up */
    lseek(fd,0,SEEK_SET); write(fd,buf,fix.smem_len);
    /* timed full-frame writes - the fbdev write() flushes to SPI */
    int N=5; long long best=1LL<<60;
    for(int i=0;i<N;i++){
        long long t0=ns(); lseek(fd,0,SEEK_SET); ssize_t w=write(fd,buf,fix.smem_len); long long t1=ns();
        long long dt=t1-t0; if(dt<best)best=dt;
        printf("  frame %d: %zd bytes %.2f ms\n",i,w,dt/1e6);
    }
    printf("best: %.2f ms -> %.1f fps\n",best/1e6,1e9/(double)best);
    return 0;
}
