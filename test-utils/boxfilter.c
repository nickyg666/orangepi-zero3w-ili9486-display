#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static long long ns(void){struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;}
int main(){
    int fbw=960, fbh=640, scale=2;
    int pw=fbw/scale, ph=fbh/scale;
    unsigned char *src=malloc(fbw*fbh*4);
    unsigned char *tr=malloc(pw*ph*2);
    memset(src,0x7F,fbw*fbh*4);
    long long t0=ns();
    for(int j=0;j<ph;j++){
        for(int i=0;i<pw;i++){
            unsigned int x0=i*scale, y0=j*scale;
            unsigned int r=0,g=0,b=0;
            for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++){
                const unsigned char *q=src+((y0+sy)*fbw+(x0+sx))*4;
                b+=q[0]; g+=q[1]; r+=q[2];
            }
            unsigned n=scale*scale;
            r=r/n; g=g/n; b=b/n;
            tr[(j*pw+i)*2]=(r&0xf8)|((g&0xe0)>>5);
            tr[(j*pw+i)*2+1]=((g&0x1c)<<3)|((b&0xf8)>>3);
        }
    }
    long long t1=ns();
    printf("box filter %dx%d -> %dx%d: %.1f ms\n", fbw,fbh,pw,ph,(t1-t0)/1e6);
    return 0;
}
