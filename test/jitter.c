#include <stdio.h>

int main() {
  char line[256];
  unsigned long long a = 0,b;
  unsigned long long difference;
  unsigned long long mindiff = -1, maxdiff = 0;
  unsigned long long total = 0;
  int samples = 0;

  while (gets(line)) {
    sscanf(line,"%Ld",&b);
    if (a) {
      difference = b - a;
      printf("difference is %Ld\n",difference);
      if (difference > maxdiff) maxdiff = difference;
      if (difference < mindiff) mindiff = difference;
      total += difference;
      samples++;
    }
    a = b;
  }
  printf("min difference is %Ld, avg %Ld, max is %Ld\n",
         mindiff,total/samples,maxdiff);
  printf("jitter is %Ld\n",maxdiff-mindiff);
}
