#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstTimeCache *tc;
  gint64 timestamp;
  guint64 location;
  gint group;
  GstTimeCacheCertainty certain;

  gst_init(&argc,&argv);

  tc = gst_timecache_new();
  g_return_if_fail(tc != NULL);

  fprintf(stderr,"current group in timecache is %d\n",gst_timecache_get_group(tc));

  // add an entry
  gst_timecache_add_entry(tc,0LL,0LL);

  // test for new entry
  if (gst_timecache_find_location(tc,0LL,&timestamp))
    fprintf(stderr,"found timestamp %Ld for location 0\n",timestamp);
  else
    fprintf(stderr,"ERROR: couldn't find timestamp for newly added entry at 0\n");

  // test for entry not there
  if (gst_timecache_find_location(tc,1024LL,&timestamp))
    fprintf(stderr,"ERROR: found timestamp %Ld for location 1024\n",timestamp);
  else
    fprintf(stderr,"no timestamp found at 1024\n");

  // add another entry
  gst_timecache_add_entry(tc,1024LL,1000000LL);

  // test for new entry
  if (gst_timecache_find_location(tc,1024LL,&timestamp))
    fprintf(stderr,"found timestamp %Ld for location 1024\n",timestamp);
  else
    fprintf(stderr,"ERROR: couldn't find timestamp for newly added entry at 1024\n");

  // test for new entry
  if (gst_timecache_find_timestamp(tc,1000000LL,&location))
    fprintf(stderr,"found location %Ld for location 1000000\n",location);
  else
    fprintf(stderr,"ERROR: couldn't find location for newly added entry at 1000000\n");


  // create a new group
  group = gst_timecache_new_group(tc);

  // add a couple entries
  gst_timecache_add_entry(tc, 2048LL,2000000LL);
  gst_timecache_add_entry(tc, 3072LL,3000000LL);

  // first test for an existing entry
  if (gst_timecache_find_timestamp(tc,1000000LL,&location))
    fprintf(stderr,"found location %Ld for location 1000000\n",location);
  else
    fprintf(stderr,"ERROR: couldn't find location for old entry at 1000000\n");

  // then test for an new entry in the current group
  if (gst_timecache_find_timestamp(tc,3000000LL,&location))
    fprintf(stderr,"found location %Ld for location 3000000\n",location);
  else
    fprintf(stderr,"ERROR: couldn't find location for old entry at 3000000\n");
}
