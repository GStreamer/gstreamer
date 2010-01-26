/*
 *
 * First, create a writer with sp_writer_create()
 * And selectes() on the socket from sp_get_fd()
 * If the socket is closed or there are errors from any function, the app
 * should call sp_close() and assume the writer is dead
 * The server calls sp_writer_accept_client() when there is something to read
 * from the server fd
 * It then needs to select() on the socket from sp_writer_get_client_fd()
 * If it gets an error on that socket, it call sp_writer_close_client().
 * If there is something to read, it calls sp_writer_recv().
 *
 * The writer allocates buffers with sp_writer_alloc_block(),
 * writes something in the buffer (retrieved with sp_writer_block_get_buf(),
 * then calls  sp_writer_send_buf() to send the buffer or a subsection to
 * the other side. When it is done with the block, it calls
 * sp_writer_free_block().
 * If alloc fails, then the server must wait for events from the clients before
 * trying again.
 *
 *
 * The clients connect with sp_client_open()
 * And select() on the fd from sp_get_fd() until there is something to read.
 * Then they must read using sp_client_recv() which will return > 0 if there
 * is a valid buffer (which is read only). It will return 0 if it is an internal
 * message and <0 if there was an error. If there was an error, one must close
 * it with sp_close(). If was valid buffer was received, the client must release
 * it with sp_client_recv_finish() when it is done reading from it.
 */


#ifndef __SHMPIPE_H__
#define __SHMPIPE_H__

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ShmClient ShmClient;
typedef struct _ShmPipe ShmPipe;
typedef struct _ShmBlock ShmBlock;

ShmPipe *sp_writer_create (const char *path, size_t size, mode_t perms);
const char *sp_writer_get_path (ShmPipe *pipe);
void sp_close (ShmPipe * self);

void sp_writer_setperms_shm (ShmPipe * self, mode_t perms);
int sp_writer_resize (ShmPipe * self, size_t size);

int sp_get_fd (ShmPipe * self);
int sp_writer_get_client_fd (ShmClient * client);

ShmBlock *sp_writer_alloc_block (ShmPipe * self, size_t size);
void sp_writer_free_block (ShmBlock *block);
int sp_writer_send_buf (ShmPipe * self, char *buf, size_t size);
char *sp_writer_block_get_buf (ShmBlock *block);

ShmClient * sp_writer_accept_client (ShmPipe * self);
void sp_writer_close_client (ShmPipe *self, ShmClient * client);
int sp_writer_recv (ShmPipe * self, ShmClient * client);

int sp_writer_pending_writes (ShmPipe * self);

ShmPipe *sp_client_open (const char *path);
unsigned long sp_client_recv (ShmPipe * self, char **buf);
int sp_client_recv_finish (ShmPipe * self, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* __SHMPIPE_H__ */
