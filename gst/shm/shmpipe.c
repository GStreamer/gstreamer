

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "shmpipe.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <assert.h>

/*
 * The protocol over the pipe is in packets
 *
 * The defined types are:
 * type 1: new shm area
 * Area length
 * Size of path (followed by path)
 *
 * type 2: Close shm area:
 * No payload
 *
 * type 3: shm buffer
 * offset
 * bufsize
 *
 * type 4: ack buffer
 * offset
 *
 * Type 4 goes from the client to the server
 * The rest are from the server to the client
 * The client should never write in the SHM
 */


#include "shmalloc.h"

enum
{
  COMMAND_NEW_SHM_AREA = 1,
  COMMAND_CLOSE_SHM_AREA = 2,
  COMMAND_NEW_BUFFER = 3,
  COMMAND_ACK_BUFFER = 4
};

typedef struct _ShmArea ShmArea;
typedef struct _ShmBuffer ShmBuffer;

struct _ShmArea
{
  int id;

  int use_count;

  int shm_fd;

  char *shm_area;
  size_t shm_area_len;

  char *shm_area_name;

  ShmAllocSpace *allocspace;

  ShmArea *next;
};

struct _ShmBuffer
{
  int use_count;

  ShmArea *shm_area;
  unsigned long offset;
  size_t size;

  ShmAllocBlock *block;

  ShmBuffer *next;

  int num_clients;
  int clients[0];
};


struct _ShmPipe
{
  int main_socket;
  char *socket_path;

  ShmArea *shm_area;

  int next_area_id;

  ShmBuffer *buffers;

  int num_clients;
  ShmClient *clients;

  mode_t perms;
};

struct _ShmClient
{
  int fd;

  ShmClient *next;
};

struct _ShmBlock
{
  ShmPipe *pipe;
  ShmArea *area;
  ShmAllocBlock *ablock;
};

struct CommandBuffer
{
  unsigned int type;
  int area_id;

  union
  {
    struct
    {
      size_t size;
      unsigned int path_size;
      /* Followed by path */
    } new_shm_area;
    struct
    {
      unsigned long offset;
      unsigned long size;
    } buffer;
    struct
    {
      unsigned long offset;
    } ack_buffer;
  } payload;
};

static ShmArea *sp_open_shm (char *path, int id, int writer, mode_t perms,
    size_t size);
static void sp_close_shm (ShmPipe * self, ShmArea * area);
static int sp_shmbuf_dec (ShmPipe * self, ShmBuffer * buf,
    ShmBuffer * prev_buf);
static void sp_shm_area_dec (ShmPipe * self, ShmArea * area);



#define RETURN_ERROR(format, ...) do {                  \
  fprintf (stderr, format, __VA_ARGS__);                \
  sp_close (self);                                      \
  return NULL; } while (0)

ShmPipe *
sp_writer_create (const char *path, size_t size, mode_t perms)
{
  ShmPipe *self = spalloc_new (ShmPipe);
  int flags;
  struct sockaddr_un sun;
  int i = 0;

  memset (self, 0, sizeof (ShmPipe));

  self->main_socket = socket (PF_UNIX, SOCK_STREAM, 0);

  if (self->main_socket < 0) {
    RETURN_ERROR ("Could not create socket (%d): %s\n", errno,
        strerror (errno));
  }

  flags = fcntl (self->main_socket, F_GETFL, 0);
  if (flags < 0) {
    RETURN_ERROR ("fcntl(F_GETFL) failed (%d): %s\n", errno, strerror (errno));
  }

  if (fcntl (self->main_socket, F_SETFL, flags | O_NONBLOCK | FD_CLOEXEC) < 0) {
    RETURN_ERROR ("fcntl(F_SETFL) failed (%d): %s\n", errno, strerror (errno));
  }

  sun.sun_family = AF_UNIX;
  strncpy (sun.sun_path, path, sizeof (sun.sun_path) - 1);

  while (bind (self->main_socket, (struct sockaddr *) &sun,
          sizeof (struct sockaddr_un)) < 0) {
    if (errno != EADDRINUSE)
      RETURN_ERROR ("bind() failed (%d): %s\n", errno, strerror (errno));

    if (i > 256)
      RETURN_ERROR ("Could not find a free socket name for %s", path);

    snprintf (sun.sun_path, sizeof (sun.sun_path), "%s.%d", path, i);
    i++;
  }

  self->socket_path = strdup (sun.sun_path);

  if (listen (self->main_socket, 10) < 0) {
    RETURN_ERROR ("listen() failed (%d): %s\n", errno, strerror (errno));
  }

  self->shm_area = sp_open_shm (NULL, ++self->next_area_id, 1, perms, size);

  self->perms = perms;

  if (!self->shm_area) {
    sp_close (self);
    return NULL;
  }

  return self;
}

#undef RETURN_ERROR

#define RETURN_ERROR(format, ...)                       \
  fprintf (stderr, format, __VA_ARGS__);                \
  sp_shm_area_dec (NULL, area);                         \
  return NULL;

static ShmArea *
sp_open_shm (char *path, int id, int writer, mode_t perms, size_t size)
{
  ShmArea *area = spalloc_new (ShmArea);
  char tmppath[PATH_MAX];
  int flags;
  int prot;
  int i = 0;

  memset (area, 0, sizeof (ShmArea));

  area->use_count = 1;

  area->shm_area_len = size;


  if (writer)
    flags = O_RDWR | O_CREAT | O_TRUNC | O_EXCL;
  else
    flags = O_RDONLY;

  area->shm_fd = -1;

  if (path) {
    area->shm_fd = shm_open (path, flags, perms);
  } else {
    do {
      snprintf (tmppath, PATH_MAX, "/shmpipe.5%d.%5d", getpid (), i++);
      area->shm_fd = shm_open (tmppath, flags, perms);
    } while (area->shm_fd < 0 && errno == EEXIST);
  }

  if (area->shm_fd < 0) {
    RETURN_ERROR ("shm_open failed on %s (%d): %s\n",
        path ? path : tmppath, errno, strerror (errno));
  }

  if (!path)
    area->shm_area_name = strdup (tmppath);

  if (writer) {
    if (ftruncate (area->shm_fd, size)) {
      RETURN_ERROR ("Could not resize memory area to header size,"
          " ftruncate failed (%d): %s\n", errno, strerror (errno));
    }
  }

  if (writer)
    prot = PROT_READ | PROT_WRITE;
  else
    prot = PROT_READ;

  area->shm_area = mmap (NULL, size, prot, MAP_SHARED, area->shm_fd, 0);

  if (area->shm_area == MAP_FAILED) {
    RETURN_ERROR ("mmap failed (%d): %s\n", errno, strerror (errno));
  }

  area->id = id;

  if (writer)
    area->allocspace = shm_alloc_space_new (area->shm_area_len);

  return area;
}

#undef RETURN_ERROR

static void
sp_close_shm (ShmPipe * self, ShmArea * area)
{
  ShmArea *item = NULL;
  ShmArea *prev_item = NULL;

  assert (area->use_count == 0);

  if (area->allocspace)
    shm_alloc_space_free (area->allocspace);


  for (item = self->shm_area; item; item = item->next) {
    if (item == area) {
      if (prev_item)
        prev_item->next = item->next;
      else
        self->shm_area = item->next;
      break;
    }
    prev_item = item;
  }
  assert (item);

  if (area->shm_area != MAP_FAILED)
    munmap (area->shm_area, area->shm_area_len);

  if (area->shm_fd >= 0)
    close (area->shm_fd);

  if (area->shm_area_name) {
    shm_unlink (area->shm_area_name);
    free (area->shm_area_name);
  }

  spalloc_free (ShmArea, area);
}

static void
sp_shm_area_inc (ShmArea * area)
{
  area->use_count++;
}

static void
sp_shm_area_dec (ShmPipe * self, ShmArea * area)
{
  assert (area->use_count > 0);
  area->use_count--;

  if (area->use_count == 0) {
    sp_close_shm (self, area);
  }
}

void
sp_close (ShmPipe * self)
{
  if (self->main_socket >= 0)
    close (self->main_socket);

  if (self->socket_path) {
    unlink (self->socket_path);
    free (self->socket_path);
  }

  while (self->clients)
    sp_writer_close_client (self, self->clients);

  while (self->shm_area) {
    sp_shm_area_dec (self, self->shm_area);
  }

  spalloc_free (ShmPipe, self);
}

void
sp_writer_setperms_shm (ShmPipe * self, mode_t perms)
{
  self->perms = perms;
  fchmod (self->shm_area->shm_fd, perms);
}

static int
send_command (int fd, struct CommandBuffer *cb, unsigned short int type,
    int area_id)
{
  cb->type = type;
  cb->area_id = area_id;

  if (send (fd, cb, sizeof (struct CommandBuffer), MSG_NOSIGNAL) !=
      sizeof (struct CommandBuffer))
    return 0;

  return 1;
}

int
sp_writer_resize (ShmPipe * self, size_t size)
{
  ShmArea *newarea;
  ShmArea *old_current;
  ShmClient *client;
  int c = 0;
  int pathlen;

  if (self->shm_area->shm_area_len == size)
    return 0;

  newarea = sp_open_shm (NULL, ++self->next_area_id, 1, self->perms, size);

  if (!newarea)
    return -1;

  old_current = self->shm_area;
  newarea->next = self->shm_area;
  self->shm_area = newarea;

  pathlen = strlen (newarea->shm_area_name) + 1;

  for (client = self->clients; client; client = client->next) {
    struct CommandBuffer cb = { 0 };

    if (!send_command (client->fd, &cb, COMMAND_CLOSE_SHM_AREA,
            old_current->id))
      continue;

    cb.payload.new_shm_area.size = newarea->shm_area_len;
    cb.payload.new_shm_area.path_size = pathlen;
    if (!send_command (client->fd, &cb, COMMAND_NEW_SHM_AREA, newarea->id))
      continue;

    if (send (client->fd, newarea->shm_area_name, pathlen, MSG_NOSIGNAL) !=
        pathlen)
      continue;
    c++;
  }

  sp_shm_area_dec (self, old_current);


  return c;
}

ShmBlock *
sp_writer_alloc_block (ShmPipe * self, size_t size)
{
  ShmBlock *block;
  ShmAllocBlock *ablock =
      shm_alloc_space_alloc_block (self->shm_area->allocspace, size);

  if (!ablock)
    return NULL;

  block = spalloc_new (ShmBlock);
  sp_shm_area_inc (self->shm_area);
  block->pipe = self;
  block->area = self->shm_area;
  block->ablock = ablock;
  return block;
}

char *
sp_writer_block_get_buf (ShmBlock * block)
{
  return block->area->shm_area +
      shm_alloc_space_alloc_block_get_offset (block->ablock);
}

void
sp_writer_free_block (ShmBlock * block)
{
  shm_alloc_space_block_dec (block->ablock);
  sp_shm_area_dec (block->pipe, block->area);
  spalloc_free (ShmBlock, block);
}

/* Returns the number of client this has successfully been sent to */

int
sp_writer_send_buf (ShmPipe * self, char *buf, size_t size)
{
  ShmArea *area = NULL;
  unsigned long offset = 0;
  unsigned long bsize = size;
  ShmBuffer *sb;
  ShmClient *client = NULL;
  ShmAllocBlock *block = NULL;
  int i = 0;
  int c = 0;

  if (self->num_clients == 0)
    return 0;

  for (area = self->shm_area; area; area = area->next) {
    if (buf >= area->shm_area && buf < (area->shm_area + area->shm_area_len)) {
      offset = buf - area->shm_area;
      block = shm_alloc_space_block_get (area->allocspace, offset);
      assert (block);
      break;
    }
  }

  if (!block)
    return -1;

  sb = spalloc_alloc (sizeof (ShmBuffer) + sizeof (int) * self->num_clients);
  memset (sb, 0, sizeof (ShmBuffer));
  memset (sb->clients, -1, sizeof (int) * self->num_clients);
  sb->shm_area = area;
  sb->offset = offset;
  sb->size = size;
  sb->num_clients = self->num_clients;
  sb->block = block;

  for (client = self->clients; client; client = client->next) {
    struct CommandBuffer cb = { 0 };
    cb.payload.buffer.offset = offset;
    cb.payload.buffer.size = bsize;
    if (!send_command (client->fd, &cb, COMMAND_NEW_BUFFER, self->shm_area->id))
      continue;
    sb->clients[i++] = client->fd;
    c++;
  }

  if (c == 0) {
    spalloc_free1 (sizeof (ShmBuffer) + sizeof (int) * self->num_clients, sb);
    return 0;
  }

  sp_shm_area_inc (area);
  shm_alloc_space_block_inc (block);

  sb->use_count = c;

  sb->next = self->buffers;
  self->buffers = sb;

  return c;
}

static int
recv_command (int fd, struct CommandBuffer *cb)
{
  int retval;

  retval = recv (fd, cb, sizeof (struct CommandBuffer), MSG_DONTWAIT);
  if (retval == sizeof (struct CommandBuffer)) {
    return 1;
  } else {
    return 0;
  }
}

unsigned long
sp_client_recv (ShmPipe * self, char **buf)
{
  char *area_name = NULL;
  ShmArea *newarea, *oldarea;
  ShmArea *area;
  struct CommandBuffer cb;
  int retval;

  if (!recv_command (self->main_socket, &cb))
    return -1;

  switch (cb.type) {
    case COMMAND_NEW_SHM_AREA:
      assert (cb.payload.new_shm_area.path_size > 0);
      assert (cb.payload.new_shm_area.size > 0);

      area_name = malloc (cb.payload.new_shm_area.path_size);
      retval = recv (self->main_socket, area_name,
          cb.payload.new_shm_area.path_size, 0);
      if (retval != cb.payload.new_shm_area.path_size) {
        free (area_name);
        return -3;
      }

      newarea = sp_open_shm (area_name, cb.area_id, 0, 0,
          cb.payload.new_shm_area.size);
      free (area_name);
      if (!newarea)
        return -4;

      oldarea = self->shm_area;
      newarea->next = self->shm_area;
      self->shm_area = newarea;
      /*
         if (oldarea)
         sp_shm_area_dec (self, oldarea);
       */
      break;

    case COMMAND_CLOSE_SHM_AREA:
      for (area = self->shm_area; area; area = area->next) {
        if (area->id == cb.area_id) {
          sp_shm_area_dec (self, area);
          break;
        }
      }
      break;

    case COMMAND_NEW_BUFFER:
      assert (buf);
      for (area = self->shm_area; area; area = area->next) {
        if (area->id == cb.area_id) {
          *buf = area->shm_area + cb.payload.buffer.offset;
          sp_shm_area_inc (area);
          return cb.payload.buffer.size;
        }
      }
      return -23;

    default:
      return -99;
  }

  return 0;
}

int
sp_writer_recv (ShmPipe * self, ShmClient * client)
{
  ShmBuffer *buf = NULL, *prev_buf = NULL;
  struct CommandBuffer cb;

  if (!recv_command (client->fd, &cb))
    return -1;

  switch (cb.type) {
    case COMMAND_ACK_BUFFER:

      for (buf = self->buffers; buf; buf = buf->next) {
        if (buf->shm_area->id == cb.area_id &&
            buf->offset == cb.payload.ack_buffer.offset) {
          sp_shmbuf_dec (self, buf, prev_buf);
          break;
        }
        prev_buf = buf;
      }

      if (!buf)
        return -2;

      break;
    default:
      return -99;
  }

  return 0;
}

int
sp_client_recv_finish (ShmPipe * self, char *buf)
{
  ShmArea *shm_area = NULL;
  unsigned long offset;
  struct CommandBuffer cb = { 0 };

  for (shm_area = self->shm_area; shm_area; shm_area = shm_area->next) {
    if (buf >= shm_area->shm_area &&
        buf < shm_area->shm_area + shm_area->shm_area_len)
      break;
  }

  assert (shm_area);

  offset = buf - shm_area->shm_area;

  sp_shm_area_dec (self, shm_area);

  cb.payload.ack_buffer.offset = offset;
  return send_command (self->main_socket, &cb, COMMAND_ACK_BUFFER,
      self->shm_area->id);
}

ShmPipe *
sp_client_open (const char *path)
{
  ShmPipe *self = spalloc_new (ShmPipe);
  struct sockaddr_un sun;

  memset (self, 0, sizeof (ShmPipe));

  self->main_socket = socket (PF_UNIX, SOCK_STREAM, 0);
  if (self->main_socket < 0) {
    sp_close (self);
    return NULL;
  }

  sun.sun_family = AF_UNIX;
  strncpy (sun.sun_path, path, sizeof (sun.sun_path) - 1);

  if (connect (self->main_socket, (struct sockaddr *) &sun,
          sizeof (struct sockaddr_un)) < 0)
    goto error;

  return self;

error:
  spalloc_free (ShmPipe, self);
  return NULL;
}


ShmClient *
sp_writer_accept_client (ShmPipe * self)
{
  ShmClient *client = NULL;
  int fd;
  struct CommandBuffer cb = { 0 };
  int pathlen = strlen (self->shm_area->shm_area_name) + 1;


  fd = accept (self->main_socket, NULL, NULL);

  if (fd < 0) {
    fprintf (stderr, "Could not client connection");
    return NULL;
  }

  cb.payload.new_shm_area.size = self->shm_area->shm_area_len;
  cb.payload.new_shm_area.path_size = pathlen;
  if (!send_command (fd, &cb, COMMAND_NEW_SHM_AREA, self->shm_area->id)) {
    fprintf (stderr, "Sending new shm area failed: %s", strerror (errno));
    goto error;
  }

  if (send (fd, self->shm_area->shm_area_name, pathlen, MSG_NOSIGNAL) !=
      pathlen) {
    fprintf (stderr, "Sending new shm area path failed: %s", strerror (errno));
    goto error;
  }

  client = spalloc_new (ShmClient);
  client->fd = fd;

  /* Prepend ot linked list */
  client->next = self->clients;
  self->clients = client;
  self->num_clients++;

  return client;

error:
  close (fd);
  return NULL;
}

static int
sp_shmbuf_dec (ShmPipe * self, ShmBuffer * buf, ShmBuffer * prev_buf)
{
  buf->use_count--;

  if (buf->use_count == 0) {
    /* Remove from linked list */
    if (prev_buf)
      prev_buf->next = buf->next;
    else
      self->buffers = buf->next;

    shm_alloc_space_block_dec (buf->block);
    sp_shm_area_dec (self, buf->shm_area);
    spalloc_free1 (sizeof (ShmBuffer) + sizeof (int) * buf->num_clients, buf);
    return 0;
  }

  return 1;
}

void
sp_writer_close_client (ShmPipe * self, ShmClient * client)
{
  ShmBuffer *buffer = NULL, *prev_buf = NULL;
  ShmClient *item = NULL, *prev_item = NULL;

  close (client->fd);

again:
  for (buffer = self->buffers; buffer; buffer = buffer->next) {
    int i;

    for (i = 0; i < buffer->num_clients; i++) {
      if (buffer->clients[i] == client->fd) {
        buffer->clients[i] = -1;
        if (!sp_shmbuf_dec (self, buffer, prev_buf))
          goto again;
        break;
      }
      prev_buf = buffer;
    }
  }

  for (item = self->clients; item; item = item->next) {
    if (item == client)
      break;
    prev_item = item;
  }
  assert (item);

  if (prev_item)
    prev_item->next = client->next;
  else
    self->clients = client->next;

  self->num_clients--;

  spalloc_free (ShmClient, client);
}

int
sp_get_fd (ShmPipe * self)
{
  return self->main_socket;
}

int
sp_writer_get_client_fd (ShmClient * client)
{
  return client->fd;
}

int
sp_writer_pending_writes (ShmPipe * self)
{
  return (self->buffers != NULL);
}

const char *
sp_writer_get_path (ShmPipe * pipe)
{
  return pipe->socket_path;
}
