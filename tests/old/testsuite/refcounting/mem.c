#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int
vmsize ()
{
  int pid, fd, size, i, mem;
  char filename[17], buf[256], *ptr, *end;

  pid = getpid ();
  snprintf (filename, 17, "/proc/%d/stat", pid);
  fd = open (filename, O_RDONLY);
  size = read (fd, buf, 240);
  ptr = buf;
  for (i = 0; i < 22; i++)
    ptr = (char *) strchr (ptr, ' ') + 1;
  end = (char *) strchr (ptr, ' ');
  *end = 0;
  sscanf (ptr, "%d", &mem);
  close (fd);
  return mem;
}
