#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gsttrace.h>

int main(int argc,char *argv[]) {
  gchar *filename = argv[1];
  int fd = open(filename,O_RDONLY);
  GstTraceEntry entry;

  while (read(fd,&entry,sizeof(entry)))
    g_print("%Ld(%ld) 0x%08lx: %s\n",entry.timestamp,entry.sequence,
            entry.data,entry.message);
}
