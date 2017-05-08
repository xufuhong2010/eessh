/* channel.h */

#ifndef CHANNEL_H_FILE
#define CHANNEL_H_FILE

#include <stdint.h>

enum SSH_CHAN_TYPE {
  SSH_CHAN_SESSION,
};

struct SSH_CHAN;
struct SSH_CONN;

#define SSH_CHAN_FD_READ  (1<<0)
#define SSH_CHAN_FD_WRITE (1<<1)

typedef int (*ssh_chan_created)(struct SSH_CHAN *chan);
typedef int (*ssh_chan_fd_ready)(struct SSH_CHAN *chan, int fd, uint8_t fd_flags);
typedef int (*ssh_chan_received)(struct SSH_CHAN *chan, void *data, size_t data_len);
typedef int (*ssh_chan_received_ext)(struct SSH_CHAN *chan, uint32_t data_type_code, void *data, size_t data_len);

struct SSH_CHAN_CONFIG {
  enum SSH_CHAN_TYPE type;
  ssh_chan_created created;
  ssh_chan_fd_ready fd_ready;
  ssh_chan_received received;
  ssh_chan_received_ext received_ext;
  void *type_config;
};

/* type_config for SSH_CHAN_SESSION */
struct SSH_CHAN_SESSION_CONFIG {
  const char *run_command;  // NULL to run default shell
  int alloc_pty;
  const char *term;
  uint32_t term_width;
  uint32_t term_height;
  /* TODO: encoded terminal modes */
};

uint32_t ssh_chan_get_num(struct SSH_CHAN  *chan);
int ssh_chan_watch_fd(struct SSH_CHAN  *chan, int fd, uint8_t enable_fd_flags, uint8_t disable_fd_flags);
void ssh_chan_close(struct SSH_CHAN  *chan);
ssize_t ssh_chan_send(struct SSH_CHAN *chan, void *data, size_t data_len);
ssize_t ssh_chan_send_ext(struct SSH_CHAN *chan, uint32_t data_type_code, void *data, size_t data_len);

#endif /* CHANNEL_H_FILE */
