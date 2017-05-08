/* channel.c */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

#include "ssh/channel_i.h"

#include "common/network_i.h"
#include "ssh/connection_i.h"

#include "common/error.h"
#include "common/debug.h"
#include "common/alloc.h"
#include "ssh/debug.h"

#define MAX_POLL_FDS  8

struct SSH_CHAN {
  struct SSH_CONN *conn;
  int closed;
  uint32_t local_num;
  uint32_t remote_num;
  uint32_t max_packet_size;
  uint32_t window_size;
  struct pollfd watch_fds[MAX_POLL_FDS];
  nfds_t num_watch_fds;

  enum SSH_CHAN_TYPE type;
  ssh_chan_created created;
  ssh_chan_fd_ready fd_ready;
  ssh_chan_received received;
  ssh_chan_received_ext received_ext;
};

static short to_pollfd_events(int chan_fd_flags)
{
  short events = 0;
  if ((chan_fd_flags & SSH_CHAN_FD_READ) != 0)
    events |= POLLIN;
  if ((chan_fd_flags & SSH_CHAN_FD_WRITE) != 0)
    events |= POLLOUT;
  return events;
}

static int update_poll_fd_events(struct pollfd *poll_fds, nfds_t *num_poll_fds, int fd, short add_events, short remove_events)
{
  int i;
  
  for (i = 0; i < *num_poll_fds; i++) {
    if (poll_fds[i].fd == fd) {
      poll_fds[i].events |= add_events;
      poll_fds[i].events &= ~remove_events;
      return 0;
    }
  }
  if (*num_poll_fds < MAX_POLL_FDS) {
    poll_fds[*num_poll_fds].fd = fd;
    poll_fds[*num_poll_fds].events = add_events & ~remove_events;
    (*num_poll_fds)++;
    return 0;
  }
  ssh_set_error("too many fds to watch");
  return -1;
}

static struct SSH_CHAN *chan_new(struct SSH_CONN *conn, const struct SSH_CHAN_CONFIG *cfg)
{
  struct SSH_CHAN *chan;
  uint32_t local_num;
  int i;

  // allocate local number
  local_num = 0;
  for (i = 0; i < conn->num_channels; i++) {
    if (conn->channels[i]->local_num == local_num) {
      local_num++;
      i = 0;
    }
  }
  
  if ((chan = ssh_alloc(sizeof(struct SSH_CHAN))) == NULL)
    return NULL;
  chan->conn = conn;
  chan->closed = 0;
  
  chan->local_num = local_num;
  chan->remote_num = 0;
  chan->max_packet_size = 0;
  chan->window_size = 0;
  chan->num_watch_fds = 0;

  chan->type = cfg->type;
  chan->created = cfg->created;
  chan->fd_ready = cfg->fd_ready;
  chan->received = cfg->received;
  chan->received_ext = cfg->received_ext;
  
  conn->channels[conn->num_channels++] = chan;
  return chan;
}

uint32_t ssh_chan_get_num(struct SSH_CHAN  *chan)
{
  return chan->local_num;
}

void ssh_chan_close(struct SSH_CHAN  *chan)
{
  chan->closed = 1;
}

static void chan_remove_closed_channels(struct SSH_CONN *conn)
{
  int i;

  for (i = 0; i < conn->num_channels; ) {
    if (conn->channels[i]->closed) {
      struct SSH_CHAN *free_chan = conn->channels[i];
      memmove(&conn->channels[i], &conn->channels[i+1], (conn->num_channels-i-1) * sizeof(struct SSH_CHANNEL *));
      conn->num_channels--;
      ssh_free(free_chan);
    } else
      i++;
  }
}

ssize_t ssh_chan_send(struct SSH_CHAN *chan, void *data, size_t data_len)
{
  ssh_set_error("ssh_chan_send() not implemented!");
  return -1;
}

ssize_t ssh_chan_send_ext(struct SSH_CHAN *chan, uint32_t data_type_code, void *data, size_t data_len)
{
  ssh_set_error("ssh_chan_send_ext() not implemented!");
  return -1;
}

int ssh_chan_watch_fd(struct SSH_CHAN  *chan, int fd, uint8_t enable_fd_flags, uint8_t disable_fd_flags)
{
  short enable_events = to_pollfd_events(enable_fd_flags);
  short disable_events = to_pollfd_events(disable_fd_flags);
  int i;

  if (update_poll_fd_events(chan->watch_fds, &chan->num_watch_fds, fd, enable_events, disable_events) < 0) {
    if (enable_fd_flags != 0) // no error if there's no space to add only disable_events
      return -1;
  }

  // remove empty watches
  for (i = 0; i < chan->num_watch_fds; ) {
    if (chan->watch_fds[i].events == 0) {
      memmove(&chan->watch_fds[i], &chan->watch_fds[i+1], (chan->num_watch_fds-i-1) * sizeof(struct pollfd));
      chan->num_watch_fds--;
    } else
      i++;
  }

  return 0;
}

static int chan_process_packets(struct SSH_CONN *conn)
{
  while (1) {
    struct SSH_BUF_READER *pack = ssh_conn_recv_packet(conn);
    if (pack != NULL) {
      // TODO: process packet
      dump_packet_reader("process packet", pack, conn->in_stream.mac_len);
      continue;
    }
    if (errno == EWOULDBLOCK)
      return 0;
    ssh_conn_close(conn);
    return -1;
  }
}

static int chan_notify_channels_watch_fds(struct SSH_CONN *conn, struct pollfd *poll_fd)
{
  int i, j;

  for (i = 0; i < conn->num_channels; i++) {
    struct SSH_CHAN *chan = conn->channels[i];
    for (j = 0; j < chan->num_watch_fds; j++) {
      //ssh_log("channnel %d has watch for fd %d with events %d, got event %d\n", chan->local_num, chan->watch_fds[j].fd, chan->watch_fds[j].events, poll_fd->revents);
      if (poll_fd->fd == chan->watch_fds[j].fd /* && (chan->watch_fds[j].events & poll_fd->revents) != 0 */) { // TODO: translate revents to fd_flags and call only if appropriate
        if (chan->fd_ready(chan, poll_fd->fd, poll_fd->revents) < 0)
          return -1;
      }
    }
  }
  return 0;
}

static int chan_loop(struct SSH_CONN *conn)
{
  struct pollfd poll_fds[MAX_POLL_FDS];
  nfds_t num_poll_fds;
  int i, j;

  while (1) {
    chan_remove_closed_channels(conn);
    if (conn->num_channels == 0)
      break;
    
    poll_fds[0].fd = conn->sock;
    poll_fds[0].events = POLLIN;
    if (ssh_conn_send_is_pending(conn))
      poll_fds[0].events |= POLLOUT;
    num_poll_fds = 1;

    for (i = 0; i < conn->num_channels; i++) {
      struct SSH_CHAN *chan = conn->channels[i];
      for (j = 0; j < chan->num_watch_fds; j++)
        update_poll_fd_events(poll_fds, &num_poll_fds, chan->watch_fds[j].fd, chan->watch_fds[j].events, 0);
    }

    ssh_log("* polling %d fds\n", (int) num_poll_fds);
    if (poll(poll_fds, num_poll_fds, -1) < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }

    if ((poll_fds[0].revents & POLLIN) != 0) {
      if (chan_process_packets(conn) < 0)
        return -1;
    }
    if ((poll_fds[0].revents & POLLOUT) != 0) {
      if (ssh_conn_send_flush(conn) < 0 && errno != EWOULDBLOCK)
        return -1;
    }

    for (i = 1; i < num_poll_fds; i++) {
      if (chan_notify_channels_watch_fds(conn, &poll_fds[i]) < 0)
        return -1;
    }
  }
  return 0;
}

int ssh_chan_run_connection(struct SSH_CONN *conn, int num_channels, const struct SSH_CHAN_CONFIG *channel_cfgs)
{
  int i;

  for (i = 0; i < num_channels; i++) {
    struct SSH_CHAN *chan = chan_new(conn, &channel_cfgs[i]);
    if (chan == NULL)
      return -1;
    // TODO: create remote channel
    if (chan->created(chan) < 0)
      return -1;
  }
  
  if (ssh_net_set_sock_blocking(conn->sock, 0) < 0)
    return -1;
  return chan_loop(conn);
}
