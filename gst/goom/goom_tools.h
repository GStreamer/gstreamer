#ifndef _GOOMTOOLS_H
#define _GOOMTOOLS_H

#define NB_RAND 0x10000

/* in graphic.c */
extern int *rand_tab;
extern unsigned short rand_pos;

#define RAND_INIT(i) \
	srand (i) ;\
	if (!rand_tab)\
		rand_tab = (int *) malloc (NB_RAND * sizeof(int)) ;\
	rand_pos = 1 ;\
	while (rand_pos != 0)\
		rand_tab [rand_pos++] = rand () ;

#define RAND()\
	(rand_tab[rand_pos = rand_pos + 1])

#define RAND_CLOSE()\
	free (rand_tab);\
	rand_tab = 0;


/*#define iRAND(i) ((guint32)((float)i * RAND()/RAND_MAX)) */
#define iRAND(i) (RAND()%i)

#endif
