#include <stdio.h>
#include <fcntl.h>

int main(int argc,char *argv[]) {
  int fd;
  int offset = 0;
  int got;
  unsigned short buf[2048];
  int i;
  int prev = 0;

  if (argc >= 2) fd = open(argv[1],O_RDONLY);
  else fd = 0;

  while (got = read(fd,buf,sizeof(buf))) {
    for (i=0;i<(got/2);i++) {
      if (buf[i] == 0x770b) {
        printf("have sync at %d (+%d)\n",offset+(i*2),(offset+(i*2))-prev);
        prev = offset+(i*2);
      }
    }
    offset += got;
  }
}
