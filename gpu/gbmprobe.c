#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "gbm.h"
#include <string.h>
int main(int argc, char**argv){
  const char *node = argv[1] ? argv[1] : "/dev/dri/renderD128";
  int fd = open(node, O_RDWR);
  if(fd<0){perror("open");return 1;}
  struct gbm_device *g = gbm_create_device(fd);
  printf("node=%s gbm=%p", node, (void*)g);
  if(g){ printf(" backend=%s\n", gbm_device_get_backend_name(g)); }
  else printf("\n");
  return 0;
}
