#include <glib.h>
#include <dummy.h>

int main(int argc,char *argv[]) {
  Dummy *driver,*passenger;

  driver = dummy_new();
  passenger = dummy_new_with_name("moron");

  g_print("created a couple of dummies, %p and %p\n",driver,passenger);
}
