#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
int main(int argc, char**argv) {
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    struct input_absinfo ai;
    ioctl(fd, EVIOCGABS(ABS_PRESSURE), &ai);
    printf("ABS_PRESSURE: min=%d max=%d fuzz=%d flat=%d\n", ai.minimum, ai.maximum, ai.fuzz, ai.flat);
    return 0;
}
