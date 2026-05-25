#ifndef NETFS_H
#define NETFS_H

#include <stdint.h>

#define NETFS_OK             0
#define NETFS_ERR_NOTFOUND  -1
#define NETFS_ERR_TOOBIG    -2
#define NETFS_ERR_TIMEOUT   -3
#define NETFS_ERR_REFUSED   -4   /* Server rejected the write */

void netfs_init(void);

/* Download a file from the server into buf.
   Returns bytes received, or negative NETFS_ERR_* code. */
int netfs_get(const char* filename, uint8_t* buf, uint32_t buf_size);

/* Request a list of all files on the server.
   Response written into buf as newline-separated filenames.
   Returns total bytes written, or negative NETFS_ERR_* code. */
int netfs_list(uint8_t* buf, uint32_t buf_size);

/* Upload buf (size bytes) to the server as filename.
   Returns NETFS_OK on success, or negative NETFS_ERR_* code. */
int netfs_put(const char* filename, const uint8_t* buf, uint32_t size);

#endif