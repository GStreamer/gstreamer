#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <gst/gst.h>

int eofflag = 0;

void eof(GstSrc *src) {
  eofflag = 1;
}

int main(int argc,char *argv[]) {
  struct sockaddr_in src_addr, dst_addr;
  int sockaddrlen;
  int lsock;
  int one = 1;
  int sndbuf = 4096;
  int sock;
  GstElement *src,*sink;

  gst_init(&argc,&argv);

  lsock = socket(AF_INET,SOCK_STREAM,0);
  if (lsock < 0) {
    perror("creating socket");
    exit(1);
  }

  if (setsockopt(lsock,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one))) {
    perror("setsockopt(SO_REUSEADDR)");
    exit(1);
  }

  src_addr.sin_family = AF_INET;
  src_addr.sin_addr.s_addr = INADDR_ANY;
  src_addr.sin_port = htons(8001);

  if (bind(lsock,(struct sockaddr *)&src_addr,sizeof(src_addr))) {
    perror("binding");
    exit(1);
  }

  if (setsockopt(lsock,SOL_SOCKET,SO_SNDBUF,(char *)&sndbuf,sizeof(sndbuf))) {
    perror("setsockopt(SO_SNDBUF)");
    exit(1);
  }

  g_print("listening\n");
  listen(lsock,8);

  sock = accept(lsock,(struct sockaddr *)&dst_addr,&sockaddrlen);
  g_print("connected\n");

  close(lsock);

  g_print("creating pipeline\n");
  src = gst_disksrc_new_with_location("src",argv[1]);
  g_print("have src\n");
  gtk_signal_connect(GTK_OBJECT(src),"eof",GTK_SIGNAL_FUNC(eof),NULL);
  g_print("have eof signal\n");
  sink = gst_fdsink_new_with_fd("sink",sock);
  g_print("have sink\n");

  g_print("connecting\n");
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(sink,"sink"));

  g_print("pushing...\n");
  while (!eofflag)
    gst_src_push(GST_SRC(src));

  sleep(1);
  close(sock);
}
