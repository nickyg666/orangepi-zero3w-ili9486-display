#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
static long long ns(void){struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;}
int main(){
    int fd=open("/dev/spidev3.1",O_RDWR);
    if(fd<0){perror("open");return 1;}
    uint32_t chunk=4096, nchunk=64; /* 256KB total */
    uint8_t *buf=malloc(chunk); memset(buf,0xAA,chunk);
    struct spi_ioc_transfer tr; memset(&tr,0,sizeof(tr));
    tr.tx_buf=(unsigned long)buf; tr.len=chunk; tr.bits_per_word=8;
    uint32_t speeds[]={1000000,4000000,8000000,16000000,32000000,48000000,64000000};
    for(unsigned i=0;i<sizeof(speeds)/sizeof(speeds[0]);i++){
        tr.speed_hz=speeds[i];
        long long t0=ns();
        for(unsigned j=0;j<nchunk;j++) if(ioctl(fd,SPI_IOC_MESSAGE(1),&tr)<0){perror("xfer");goto next;}
        {long long t1=ns(); double dt=(t1-t0)/1e9;
        printf("%8u Hz: %.1f MB/s\n", speeds[i], (double)(chunk*nchunk)/dt/1e6);}
        next: ;
    }
    close(fd); return 0;
}
