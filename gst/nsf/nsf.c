/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nsf.c
**
** NSF loading/saving related functions
** $Id$
*/


#include <stdio.h>
#include <string.h>
#include "types.h"
#include "nsf.h"
#include "log.h"
#include "nes6502.h"
#include "nes_apu.h"
#include "vrcvisnd.h"
#include "vrc7_snd.h"
#include "mmc5_snd.h"
#include "fds_snd.h"

/* TODO: bleh! should encapsulate in NSF */
#define  MAX_ADDRESS_HANDLERS    32
static nes6502_memread nsf_readhandler[MAX_ADDRESS_HANDLERS];
static nes6502_memwrite nsf_writehandler[MAX_ADDRESS_HANDLERS];

static nsf_t *cur_nsf = NULL;

static void
nsf_setcontext (nsf_t * nsf)
{
  ASSERT (nsf);
  cur_nsf = nsf;
}

static uint8
read_mirrored_ram (uint32 address)
{
  nes6502_chk_mem_access (&cur_nsf->cpu->acc_mem_page[0][address & 0x7FF],
      NES6502_READ_ACCESS);
  return cur_nsf->cpu->mem_page[0][address & 0x7FF];
}

static void
write_mirrored_ram (uint32 address, uint8 value)
{
  nes6502_chk_mem_access (&cur_nsf->cpu->acc_mem_page[0][address & 0x7FF],
      NES6502_WRITE_ACCESS);
  cur_nsf->cpu->mem_page[0][address & 0x7FF] = value;
}

/* can be used for both banked and non-bankswitched NSFs */
static void
nsf_bankswitch (uint32 address, uint8 value)
{
  int cpu_page;
  int roffset;
  uint8 *offset;

  cpu_page = address & 0x0F;
  roffset = -(cur_nsf->load_addr & 0x0FFF) + ((int) value << 12);
  offset = cur_nsf->data + roffset;

  nes6502_getcontext (cur_nsf->cpu);
  cur_nsf->cpu->mem_page[cpu_page] = offset;
#ifdef NES6502_MEM_ACCESS_CTRL
  cur_nsf->cpu->acc_mem_page[cpu_page] = offset + cur_nsf->length;
#endif
  nes6502_setcontext (cur_nsf->cpu);
}

static nes6502_memread default_readhandler[] = {
  {0x0800, 0x1FFF, read_mirrored_ram},
  {0x4000, 0x4017, apu_read},
  {(uint32) - 1, (uint32) - 1, NULL}
};

static nes6502_memwrite default_writehandler[] = {
  {0x0800, 0x1FFF, write_mirrored_ram},
  {0x4000, 0x4017, apu_write},
  {0x5FF6, 0x5FFF, nsf_bankswitch},
  {(uint32) - 1, (uint32) - 1, NULL}
};

static uint8
invalid_read (uint32 address)
{
#ifdef NOFRENDO_DEBUG
  log_printf ("filthy NSF read from $%04X\n", address);
#endif /* NOFRENDO_DEBUG */

  return 0xFF;
}

static void
invalid_write (uint32 address, uint8 value)
{
#ifdef NOFRENDO_DEBUG
  log_printf ("filthy NSF tried to write $%02X to $%04X\n", value, address);
#endif /* NOFRENDO_DEBUG */
}

/* set up the address handlers that the CPU uses */
static void
build_address_handlers (nsf_t * nsf)
{
  int count, num_handlers;

  memset (nsf_readhandler, 0, sizeof (nsf_readhandler));
  memset (nsf_writehandler, 0, sizeof (nsf_writehandler));

  num_handlers = 0;
  for (count = 0; num_handlers < MAX_ADDRESS_HANDLERS; count++, num_handlers++) {
    if (NULL == default_readhandler[count].read_func)
      break;

    memcpy (&nsf_readhandler[num_handlers], &default_readhandler[count],
        sizeof (nes6502_memread));
  }

  if (nsf->apu->ext) {
    if (NULL != nsf->apu->ext->mem_read) {
      for (count = 0; num_handlers < MAX_ADDRESS_HANDLERS;
          count++, num_handlers++) {
        if (NULL == nsf->apu->ext->mem_read[count].read_func)
          break;

        memcpy (&nsf_readhandler[num_handlers], &nsf->apu->ext->mem_read[count],
            sizeof (nes6502_memread));
      }
    }
  }

  /* catch-all for bad reads */
  nsf_readhandler[num_handlers].min_range = 0x2000;     /* min address */
  nsf_readhandler[num_handlers].max_range = 0x5BFF;     /* max address */
  nsf_readhandler[num_handlers].read_func = invalid_read;       /* handler */
  num_handlers++;
  nsf_readhandler[num_handlers].min_range = -1;
  nsf_readhandler[num_handlers].max_range = -1;
  nsf_readhandler[num_handlers].read_func = NULL;
  num_handlers++;
  ASSERT (num_handlers <= MAX_ADDRESS_HANDLERS);

  num_handlers = 0;
  for (count = 0; num_handlers < MAX_ADDRESS_HANDLERS; count++, num_handlers++) {
    if (NULL == default_writehandler[count].write_func)
      break;

    memcpy (&nsf_writehandler[num_handlers], &default_writehandler[count],
        sizeof (nes6502_memwrite));
  }

  if (nsf->apu->ext) {
    if (NULL != nsf->apu->ext->mem_write) {
      for (count = 0; num_handlers < MAX_ADDRESS_HANDLERS;
          count++, num_handlers++) {
        if (NULL == nsf->apu->ext->mem_write[count].write_func)
          break;

        memcpy (&nsf_writehandler[num_handlers],
            &nsf->apu->ext->mem_write[count], sizeof (nes6502_memwrite));
      }
    }
  }

  /* catch-all for bad writes */
  nsf_writehandler[num_handlers].min_range = 0x2000;    /* min address */
  nsf_writehandler[num_handlers].max_range = 0x5BFF;    /* max address */
  nsf_writehandler[num_handlers].write_func = invalid_write;    /* handler */
  num_handlers++;
  /* protect region at $8000-$FFFF */
  nsf_writehandler[num_handlers].min_range = 0x8000;    /* min address */
  nsf_writehandler[num_handlers].max_range = 0xFFFF;    /* max address */
  nsf_writehandler[num_handlers].write_func = invalid_write;    /* handler */
  num_handlers++;
  nsf_writehandler[num_handlers].min_range = -1;
  nsf_writehandler[num_handlers].max_range = -1;
  nsf_writehandler[num_handlers].write_func = NULL;
  num_handlers++;
  ASSERT (num_handlers <= MAX_ADDRESS_HANDLERS);
}

#define  NSF_ROUTINE_LOC   0x5000

/* sets up a simple loop that calls the desired routine and spins */
static void
nsf_setup_routine (uint32 address, uint8 a_reg, uint8 x_reg)
{
  uint8 *mem;

  nes6502_getcontext (cur_nsf->cpu);
  mem =
      cur_nsf->cpu->mem_page[NSF_ROUTINE_LOC >> 12] +
      (NSF_ROUTINE_LOC & 0x0FFF);

  /* our lovely 4-byte 6502 NSF player */
  mem[0] = 0x20;                /* JSR address */
  mem[1] = address & 0xFF;
  mem[2] = address >> 8;
  mem[3] = 0xF2;                /* JAM (cpu kill op) */

  cur_nsf->cpu->pc_reg = NSF_ROUTINE_LOC;
  cur_nsf->cpu->a_reg = a_reg;
  cur_nsf->cpu->x_reg = x_reg;
  cur_nsf->cpu->y_reg = 0;
  cur_nsf->cpu->s_reg = 0xFF;

  nes6502_setcontext (cur_nsf->cpu);
}

/* retrieve any external soundchip driver */
static apuext_t *
nsf_getext (nsf_t * nsf)
{
  switch (nsf->ext_sound_type) {
    case EXT_SOUND_VRCVI:
      return &vrcvi_ext;

    case EXT_SOUND_VRCVII:
      return &vrc7_ext;

    case EXT_SOUND_FDS:
      return &fds_ext;

    case EXT_SOUND_MMC5:
      return &mmc5_ext;

    case EXT_SOUND_NAMCO106:
    case EXT_SOUND_SUNSOFT_FME07:
    case EXT_SOUND_NONE:
    default:
      return NULL;
  }
}

static void
nsf_inittune (nsf_t * nsf)
{
  uint8 bank, x_reg;
  uint8 start_bank, num_banks;

  memset (nsf->cpu->mem_page[0], 0, 0x800);
  memset (nsf->cpu->mem_page[6], 0, 0x1000);
  memset (nsf->cpu->mem_page[7], 0, 0x1000);

#ifdef NES6502_MEM_ACCESS_CTRL
  memset (nsf->cpu->acc_mem_page[0], 0, 0x800);
  memset (nsf->cpu->acc_mem_page[6], 0, 0x1000);
  memset (nsf->cpu->acc_mem_page[7], 0, 0x1000);
  memset (nsf->data + nsf->length, 0, nsf->length);
#endif
  nsf->cur_frame = 0;
/*    nsf->last_access_frame = 0; */
  nsf->cur_frame_end = !nsf->song_frames
      ? 0 : nsf->song_frames[nsf->current_song];

  if (nsf->bankswitched) {
    /* the first hack of the NSF spec! */
    if (EXT_SOUND_FDS == nsf->ext_sound_type) {
      nsf_bankswitch (0x5FF6, nsf->bankswitch_info[6]);
      nsf_bankswitch (0x5FF7, nsf->bankswitch_info[7]);
    }

    for (bank = 0; bank < 8; bank++)
      nsf_bankswitch (0x5FF8 + bank, nsf->bankswitch_info[bank]);
  } else {
    /* not bankswitched, just page in our standard stuff */
    ASSERT (nsf->load_addr + nsf->length <= 0x10000);

    /* avoid ripper filth */
    for (bank = 0; bank < 8; bank++)
      nsf_bankswitch (0x5FF8 + bank, bank);

    start_bank = nsf->load_addr >> 12;
    num_banks = ((nsf->load_addr + nsf->length - 1) >> 12) - start_bank + 1;

    for (bank = 0; bank < num_banks; bank++)
      nsf_bankswitch (0x5FF0 + start_bank + bank, bank);
  }

  /* determine PAL/NTSC compatibility shite */
  if (nsf->pal_ntsc_bits & NSF_DEDICATED_PAL)
    x_reg = 1;
  else
    x_reg = 0;

  /* execute 1 frame or so; let init routine run free */
  nsf_setup_routine (nsf->init_addr, (uint8) (nsf->current_song - 1), x_reg);
  nes6502_execute ((int) NES_FRAME_CYCLES);
}

void
nsf_frame (nsf_t * nsf)
{
  /* This is how Matthew Conte left it */
  /* nsf_setcontext(nsf); *//* future expansion =) */

  /* This was suggested by Arne Morten Kvarving, who says: */
/*	Also, I fixed a bug that prevented Nosefart to play multiple tunes at
	one time (actually it was just a few missing setcontext calls in the
	playback routine, it had a nice TODO commented beside it. You had to set
	the cpu and apu contexts not just the nsf context).

	it will affect any player that tries to use nosefart to play more than one
	tune at a time.
*/
  nsf_setcontext (nsf);
  apu_setcontext (nsf->apu);
  nes6502_setcontext (nsf->cpu);

  /* one frame of NES processing */
  nsf_setup_routine (nsf->play_addr, 0, 0);
  nes6502_execute ((int) NES_FRAME_CYCLES);

  ++nsf->cur_frame;
#if defined(NES6502_MEM_ACCESS_CTRL) && 0
  if (nes6502_mem_access) {
    uint32 sec =
        (nsf->last_access_frame + nsf->playback_rate - 1) / nsf->playback_rate;
    nsf->last_access_frame = nsf->cur_frame;
    fprintf (stderr, "nsf : memory access [%x] at frame #%u [%u:%02u]\n",
        nes6502_mem_access, nsf->last_access_frame, sec / 60, sec % 60);
  }
#endif

}

/* Deallocate memory */
static void
nes_shutdown (nsf_t * nsf)
{
  int i;

  ASSERT (nsf);

  if (nsf->cpu) {
    if (nsf->cpu->mem_page[0]) {
      free (nsf->cpu->mem_page[0]);     /*tracks 1 and 2 of lifeforce hang here. */
    }
    for (i = 5; i <= 7; i++) {
      if (nsf->cpu->mem_page[i]) {
        free (nsf->cpu->mem_page[i]);
      }
    }

#ifdef NES6502_MEM_ACCESS_CTRL
    if (nsf->cpu->acc_mem_page[0]) {
      free (nsf->cpu->acc_mem_page[0]);
    }
    for (i = 5; i <= 7; i++) {
      if (nsf->cpu->acc_mem_page[i]) {
        free (nsf->cpu->acc_mem_page[i]);
      }
    }
#endif
    free (nsf->cpu);
  }
}

int
nsf_init (void)
{
  nes6502_init ();
  return 0;
}

/* Initialize NES CPU, hardware, etc. */
static int
nsf_cpuinit (nsf_t * nsf)
{
  int i;

  nsf->cpu = malloc (sizeof (nes6502_context));
  if (NULL == nsf->cpu)
    return -1;

  memset (nsf->cpu, 0, sizeof (nes6502_context));

  nsf->cpu->mem_page[0] = malloc (0x800);
  if (NULL == nsf->cpu->mem_page[0])
    return -1;

  /* allocate some space for the NSF "player" MMC5 EXRAM, and WRAM */
  for (i = 5; i <= 7; i++) {
    nsf->cpu->mem_page[i] = malloc (0x1000);
    if (NULL == nsf->cpu->mem_page[i])
      return -1;
  }

#ifdef NES6502_MEM_ACCESS_CTRL
  nsf->cpu->acc_mem_page[0] = malloc (0x800);
  if (NULL == nsf->cpu->acc_mem_page[0])
    return -1;
  /* allocate some space for the NSF "player" MMC5 EXRAM, and WRAM */
  for (i = 5; i <= 7; i++) {
    nsf->cpu->acc_mem_page[i] = malloc (0x1000);
    if (NULL == nsf->cpu->acc_mem_page[i])
      return -1;
  }
#endif

  nsf->cpu->read_handler = nsf_readhandler;
  nsf->cpu->write_handler = nsf_writehandler;

  return 0;
}

static unsigned int
nsf_playback_rate (nsf_t * nsf)
{
  if (nsf->pal_ntsc_bits & NSF_DEDICATED_PAL) {
    if (nsf->pal_speed)
      nsf->playback_rate = 1000000 / nsf->pal_speed;
    else
      nsf->playback_rate = 50;  /* 50 Hz */
  } else {
    if (nsf->ntsc_speed)
      nsf->playback_rate = 1000000 / nsf->ntsc_speed;
    else
      nsf->playback_rate = 60;  /* 60 Hz */
  }
  return 0;
}

static void
nsf_setup (nsf_t * nsf)
{
  int i;

  nsf->current_song = nsf->start_song;
  nsf_playback_rate (nsf);

  nsf->bankswitched = FALSE;
  for (i = 0; i < 8; i++) {
    if (nsf->bankswitch_info[i]) {
      nsf->bankswitched = TRUE;
      break;
    }
  }
}

#ifdef HOST_LITTLE_ENDIAN
#define  SWAP_16(x)  (x)
#else /* !HOST_LITTLE_ENDIAN */
#define  SWAP_16(x)  (((uint16) x >> 8) | (((uint16) x & 0xFF) << 8))
#endif /* !HOST_LITTLE_ENDIAN */

/* $$$ ben : find extension. Should be OK with DOS, but not with some
 * OS like RiscOS ... */
static char *
find_ext (char *fn)
{
  char *a, *b, *c;

  a = strrchr (fn, '.');
  b = strrchr (fn, '/');
  c = strrchr (fn, '\\');
  if (a <= b || a <= c) {
    a = 0;
  }
  return a;
}

/* $$$ ben : FILE loader */
struct nsf_file_loader_t
{
  struct nsf_loader_t loader;
  FILE *fp;
  char *fname;
  int name_allocated;
};

static int
nfs_open_file (struct nsf_loader_t *loader)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;

  floader->name_allocated = 0;
  floader->fp = 0;
  if (!floader->fname) {
    return -1;
  }
  floader->fp = fopen (floader->fname, "rb");
  if (!floader->fp) {
    char *fname, *ext;

    ext = find_ext (floader->fname);
    if (ext) {
      /* There was an extension, so we do not change it */
      return -1;
    }
    fname = malloc (strlen (floader->fname) + 5);
    if (!fname) {
      return -1;
    }
    /* try with .nsf extension. */
    strcpy (fname, floader->fname);
    strcat (fname, ".nsf");
    floader->fp = fopen (fname, "rb");
    if (!floader->fp) {
      free (fname);
      return -1;
    }
    floader->fname = fname;
    floader->name_allocated = 1;
  }
  return 0;
}

static void
nfs_close_file (struct nsf_loader_t *loader)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;

  if (floader->fp) {
    fclose (floader->fp);
    floader->fp = 0;
  }
  if (floader->fname && floader->name_allocated) {
    free (floader->fname);
    floader->fname = 0;
    floader->name_allocated = 0;
  }
}

static int
nfs_read_file (struct nsf_loader_t *loader, void *data, int n)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;
  int r = fread (data, 1, n, floader->fp);

  if (r >= 0) {
    r = n - r;
  }
  return r;
}

static int
nfs_length_file (struct nsf_loader_t *loader)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;
  long save, pos;

  save = ftell (floader->fp);
  fseek (floader->fp, 0, SEEK_END);
  pos = ftell (floader->fp);
  fseek (floader->fp, save, SEEK_SET);
  return pos;
}

static int
nfs_skip_file (struct nsf_loader_t *loader, int n)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;
  int r;

  r = fseek (floader->fp, n, SEEK_CUR);
  return r;
}

static const char *
nfs_fname_file (struct nsf_loader_t *loader)
{
  struct nsf_file_loader_t *floader = (struct nsf_file_loader_t *) loader;

  return floader->fname ? floader->fname : "<null>";
}

static struct nsf_file_loader_t nsf_file_loader = {
  {
        nfs_open_file,
        nfs_close_file,
        nfs_read_file,
        nfs_length_file,
        nfs_skip_file,
      nfs_fname_file},
  0, 0, 0
};

struct nsf_mem_loader_t
{
  struct nsf_loader_t loader;
  uint8 *data;
  unsigned long cur;
  unsigned long len;
  char fname[32];
};

static int
nfs_open_mem (struct nsf_loader_t *loader)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;

  if (!mloader->data) {
    return -1;
  }
  mloader->cur = 0;
  sprintf (mloader->fname, "<mem(%p,%u)>",
      mloader->data, (unsigned int) mloader->len);
  return 0;
}

static void
nfs_close_mem (struct nsf_loader_t *loader)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;

  mloader->data = 0;
  mloader->cur = 0;
  mloader->len = 0;
}

static int
nfs_read_mem (struct nsf_loader_t *loader, void *data, int n)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;
  int rem;

  if (n <= 0) {
    return n;
  }
  if (!mloader->data) {
    return -1;
  }
  rem = mloader->len - mloader->cur;
  if (rem > n) {
    rem = n;
  }
  memcpy (data, mloader->data + mloader->cur, rem);
  mloader->cur += rem;
  return n - rem;
}

static int
nfs_length_mem (struct nsf_loader_t *loader)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;

  return mloader->len;
}

static int
nfs_skip_mem (struct nsf_loader_t *loader, int n)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;
  unsigned long goal = mloader->cur + n;

  mloader->cur = (goal > mloader->len) ? mloader->len : goal;
  return goal - mloader->cur;
}

/* FIXME: not used anywhere */
#if 0
static const char *
nfs_fname_mem (struct nsf_loader_t *loader)
{
  struct nsf_mem_loader_t *mloader = (struct nsf_mem_loader_t *) loader;

  return mloader->fname;
}
#endif

static struct nsf_mem_loader_t nsf_mem_loader = {
  {nfs_open_mem, nfs_close_mem, nfs_read_mem, nfs_length_mem, nfs_skip_mem},
  0, 0, 0
};

nsf_t *
nsf_load_extended (struct nsf_loader_t *loader)
{
  nsf_t *temp_nsf = 0;
  int length;
  char id[6];

  struct
  {
    uint8 magic[4];             /* always "NESM" */
    uint8 type[4];              /* defines extension type */
    uint8 size[4];              /* extension data size (this struct include) */
  } nsf_file_ext;

  /* no loader ! */
  if (!loader) {
    return NULL;
  }

  /* Open the "file" */
  if (loader->open (loader) < 0) {
    return NULL;
  }

  /* Get file size, and exit if there is not enough data for NSF header
   * and more since it does not make sens to have header without data.
   */
  length = loader->length (loader);
  /* For version 2, we do not need file length. just check error later. */
#if 0
  if (length <= NSF_HEADER_SIZE) {
    log_printf ("nsf : [%s] not an NSF format file\n", loader->fname (loader));
    goto error;
  }
#endif

  /* Read magic */
  if (loader->read (loader, id, 5)) {
    log_printf ("nsf : [%s] error reading magic number\n",
        loader->fname (loader));
    goto error;
  }

  /* Check magic */
  if (memcmp (id, NSF_MAGIC, 5)) {
    log_printf ("nsf : [%s] is not an NSF format file\n",
        loader->fname (loader));
    goto error;
  }

  /* $$$ ben : Now the file should be an NSF, we can start allocating.
   * first : the nsf struct
   */
  temp_nsf = malloc (sizeof (nsf_t));

  if (NULL == temp_nsf) {
    log_printf ("nsf : [%s] error allocating nsf header\n",
        loader->fname (loader));
    goto error;
  }
  /* $$$ ben : safety net */
  memset (temp_nsf, 0, sizeof (nsf_t));
  /* Copy magic ID */
  memcpy (temp_nsf, id, 5);

  /* Read header (without MAGIC) */
  if (loader->read (loader, (int8 *) temp_nsf + 5, NSF_HEADER_SIZE - 5)) {
    log_printf ("nsf : [%s] error reading nsf header\n",
        loader->fname (loader));
    goto error;
  }

  /* fixup endianness */
  temp_nsf->load_addr = SWAP_16 (temp_nsf->load_addr);
  temp_nsf->init_addr = SWAP_16 (temp_nsf->init_addr);
  temp_nsf->play_addr = SWAP_16 (temp_nsf->play_addr);
  temp_nsf->ntsc_speed = SWAP_16 (temp_nsf->ntsc_speed);
  temp_nsf->pal_speed = SWAP_16 (temp_nsf->pal_speed);

  /* we're now at position 80h */


  /* Here comes the specific codes for spec version 2 */

  temp_nsf->length = 0;

  if (temp_nsf->version > 1) {
    /* Get specified data size in reserved field (3 bytes). */
    temp_nsf->length = 0 + temp_nsf->reserved[0]
        + (temp_nsf->reserved[1] << 8)
        + (temp_nsf->reserved[2] << 16);

  }
  /* no specified size : try to guess with file length. */
  if (!temp_nsf->length) {
    temp_nsf->length = length - NSF_HEADER_SIZE;
  }

  if (temp_nsf->length <= 0) {
    log_printf ("nsf : [%s] not an NSF format file (missing data)\n",
        loader->fname (loader));
    goto error;
  }

  /* Allocate NSF space, and load it up! */
  {
    int len = temp_nsf->length;

#ifdef NES6502_MEM_ACCESS_CTRL
    /* $$$ twice memory for access control shadow mem. */
    len <<= 1;
#endif
    temp_nsf->data = malloc (len);
  }
  if (NULL == temp_nsf->data) {
    log_printf ("nsf : [%s] error allocating nsf data\n",
        loader->fname (loader));
    goto error;
  }

  /* Read data */
  if (loader->read (loader, temp_nsf->data, temp_nsf->length)) {
    log_printf ("nsf : [%s] error reading NSF data\n", loader->fname (loader));
    goto error;
  }

  /* Here comes the second part of spec > 1 : get extension */
  while (!loader->read (loader, &nsf_file_ext, sizeof (nsf_file_ext))
      && !memcmp (nsf_file_ext.magic, id, 4)) {
    /* Got a NESM extension here. Checks for known extension type :
     * right now, the only extension is "TIME" which give songs length.
     * in frames.
     */
    int size;

    size = 0 + nsf_file_ext.size[0]
        + (nsf_file_ext.size[1] << 8)
        + (nsf_file_ext.size[2] << 16)
        + (nsf_file_ext.size[3] << 24);

    if (size < sizeof (nsf_file_ext)) {
      log_printf ("nsf : [%s] corrupt extension size (%d)\n",
          loader->fname (loader), size);
      /* Not a fatal error here. Just skip extension loading. */
      break;
    }
    size -= sizeof (nsf_file_ext);

    if (!temp_nsf->song_frames && !memcmp (nsf_file_ext.type, "TIME", 4)
        && !(size & 3)
        && (size >= 2 * 4)
        && (size <= 256 * 4)) {

      uint8 tmp_time[256][4];
      int tsongs = size >> 2;
      int i;
      int songs = temp_nsf->num_songs;

      /* Add 1 for 0 which contains total time for all songs. */
      ++songs;

      if (loader->read (loader, tmp_time, size)) {
        log_printf ("nsf : [%s] missing extension data\n",
            loader->fname (loader));
        /* Not a fatal error here. Just skip extension loading. */
        break;
      }
      /* Alloc song_frames for songs (not tsongs). */
      temp_nsf->song_frames = malloc (sizeof (*temp_nsf->song_frames) * songs);
      if (!temp_nsf->song_frames) {
        log_printf ("nsf : [%s] extension alloc failed\n",
            loader->fname (loader));
        /* Not a fatal error here. Just skip extension loading. */
        break;
      }

      if (tsongs > songs) {
        tsongs = songs;
      }

      /* Copy time info. */
      for (i = 0; i < tsongs; ++i) {
        temp_nsf->song_frames[i] = 0 | tmp_time[i][0]
            | (tmp_time[i][1] << 8)
            | (tmp_time[i][2] << 16)
            | (tmp_time[i][2] << 24);
      }
      /* Clear missing (safety net). */
      for (; i < songs; ++i) {
        temp_nsf->song_frames[i] = 0;
      }
    } else if (loader->skip (loader, size)) {
      log_printf ("nsf : [%s] extension skip failed\n", loader->fname (loader));
      /* Not a fatal error here. Just skip extension loading. */
      break;
    }
  }


  /* Close "file" */
  loader->close (loader);
  loader = 0;

  /* Set up some variables */
  nsf_setup (temp_nsf);
  temp_nsf->apu = NULL;         /* just make sure */

  if (nsf_cpuinit (temp_nsf)) {
    log_printf ("nsf : error cpu init\n");
    goto error;
  }
  return temp_nsf;

  /* $$$ ben : some people tell that goto are not clean. I am not agree with
   * them. In most case, it allow to avoid code duplications, which are as
   * most people know a source of error... Here we are sure of being clean
   */
error:
  if (loader) {
    loader->close (loader);
  }
  if (temp_nsf) {
    nsf_free (&temp_nsf);
  }
  return 0;
}

/* Load a ROM image into memory */
nsf_t *
nsf_load (const char *filename, void *source, int length)
{
  struct nsf_loader_t *loader = 0;

  /* $$$ ben : new loader */
  if (filename) {
    nsf_file_loader.fname = (char *) filename;
    loader = &nsf_file_loader.loader;
  } else {
    nsf_mem_loader.data = source;
    nsf_mem_loader.len = length;
    nsf_mem_loader.fname[0] = 0;
    loader = &nsf_mem_loader.loader;
  }
  return nsf_load_extended (loader);
}

/* Free an NSF */
void
nsf_free (nsf_t ** pnsf)
{
  nsf_t *nsf;

  if (!pnsf) {
    return;
  }

  nsf = *pnsf;
  /* $$$ ben : Don't see why passing a pointer to pointer
   *  is not to clear it :) */
  *pnsf = 0;

  if (nsf) {
    if (nsf->apu)
      apu_destroy (nsf->apu);

    nes_shutdown (nsf);

    if (nsf->data)
      free (nsf->data);

    if (nsf->song_frames)
      free (nsf->song_frames);

    free (nsf);
  }
}

int
nsf_setchan (nsf_t * nsf, int chan, boolean enabled)
{
  if (!nsf)
    return -1;

  nsf_setcontext (nsf);
  return apu_setchan (chan, enabled);
}

int
nsf_playtrack (nsf_t * nsf, int track, int sample_rate, int sample_bits,
    boolean stereo)
{
  if (!nsf) {
    return -1;
  }

  /* make this NSF the current context */
  nsf_setcontext (nsf);

  /* create the APU */
  if (nsf->apu) {
    apu_destroy (nsf->apu);
  }

  nsf->apu = apu_create (sample_rate, nsf->playback_rate, sample_bits, stereo);
  if (NULL == nsf->apu) {
    /* $$$ ben : from my point of view this is not clean. Function should
     *        never destroy object it has not created...
     */
    /*       nsf_free(&nsf); */
    return -1;
  }

  apu_setext (nsf->apu, nsf_getext (nsf));

  /* go ahead and init all the read/write handlers */
  build_address_handlers (nsf);

  /* convenience? */
  nsf->process = nsf->apu->process;

  nes6502_setcontext (nsf->cpu);

  if (track > nsf->num_songs)
    track = nsf->num_songs;
  else if (track < 1)
    track = 1;

  nsf->current_song = track;

  apu_reset ();

  nsf_inittune (nsf);

  return nsf->current_song;
}

int
nsf_setfilter (nsf_t * nsf, int filter_type)
{
  if (!nsf) {
    return -1;
  }
  nsf_setcontext (nsf);
  return apu_setfilter (filter_type);
}

/*
** $Log$
** Revision 1.6  2008/03/26 07:40:55  slomo
** * gst/nsf/Makefile.am:
** * gst/nsf/fds_snd.c:
** * gst/nsf/mmc5_snd.c:
** * gst/nsf/nsf.c:
** * gst/nsf/types.h:
** * gst/nsf/vrc7_snd.c:
** * gst/nsf/vrcvisnd.c:
** * gst/nsf/memguard.c:
** * gst/nsf/memguard.h:
** Remove memguard again and apply hopefully all previously dropped
** local patches. Should be really better than the old version now.
**
** Revision 1.5  2008-03-25 15:56:12  slomo
** Patch by: Andreas Henriksson <andreas at fatal dot set>
** * gst/nsf/Makefile.am:
** * gst/nsf/dis6502.h:
** * gst/nsf/fds_snd.c:
** * gst/nsf/fds_snd.h:
** * gst/nsf/fmopl.c:
** * gst/nsf/fmopl.h:
** * gst/nsf/gstnsf.c:
** * gst/nsf/log.c:
** * gst/nsf/log.h:
** * gst/nsf/memguard.c:
** * gst/nsf/memguard.h:
** * gst/nsf/mmc5_snd.c:
** * gst/nsf/mmc5_snd.h:
** * gst/nsf/nes6502.c:
** * gst/nsf/nes6502.h:
** * gst/nsf/nes_apu.c:
** * gst/nsf/nes_apu.h:
** * gst/nsf/nsf.c:
** * gst/nsf/nsf.h:
** * gst/nsf/osd.h:
** * gst/nsf/types.h:
** * gst/nsf/vrc7_snd.c:
** * gst/nsf/vrc7_snd.h:
** * gst/nsf/vrcvisnd.c:
** * gst/nsf/vrcvisnd.h:
** Update our internal nosefart to nosefart-2.7-mls to fix segfaults
** on some files. Fixes bug #498237.
** Remove some // comments, fix some compiler warnings and use pow()
** instead of a slow, selfmade implementation.
**
** Revision 1.3  2003/05/01 22:34:20  benjihan
** New NSF plugin
**
** Revision 1.2  2003/04/09 14:50:32  ben
** Clean NSF api.
**
** Revision 1.1  2003/04/08 20:53:00  ben
** Adding more files...
**
** Revision 1.14  2000/07/05 14:54:45  matt
** fix for naughty Crystalis rip
**
** Revision 1.13  2000/07/04 04:59:38  matt
** removed DOS-specific stuff, fixed bug in address handlers
**
** Revision 1.12  2000/07/03 02:19:36  matt
** dynamic address range handlers, cleaner and faster
**
** Revision 1.11  2000/06/23 03:27:58  matt
** cleaned up external sound inteface
**
** Revision 1.10  2000/06/20 20:42:47  matt
** accuracy changes
**
** Revision 1.9  2000/06/20 00:05:58  matt
** changed to driver-based external sound generation
**
** Revision 1.8  2000/06/13 03:51:54  matt
** update API to take freq/sample data on nsf_playtrack
**
** Revision 1.7  2000/06/12 03:57:14  matt
** more robust checking for winamp plugin
**
** Revision 1.6  2000/06/12 01:13:00  matt
** added CPU/APU as members of the nsf struct
**
** Revision 1.5  2000/06/11 16:09:21  matt
** nsf_free is more robust
**
** Revision 1.4  2000/06/09 15:12:26  matt
** initial revision
**
*/
