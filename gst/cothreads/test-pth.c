#include "pth_p.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>


pth_mctx_t main_context;

void thread_1 (void)
{
  printf ("sleeping 5s in thread 1...\n");
  sleep (5);
  printf ("returning to thread 0\n");
  pth_mctx_restore (&main_context);
}

int main (int argc, char *argv[])
{
  pth_mctx_t ctx;
  char *skaddr;
  
  pth_mctx_save (&main_context);
  
  skaddr = malloc (64 * 1024);
  
  pth_mctx_set (&ctx, thread_1, skaddr, skaddr + 64 * 1024);
  
  printf ("switching to thread 1...");
  
  pth_mctx_switch (&main_context, &ctx);

  printf ("back now, exiting.\n");
  
  exit (0);
}

