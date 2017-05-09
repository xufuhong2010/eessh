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
#include "ssh/ssh_constants.h"

#define MAX_POLL_FDS  8

enum CHAN_STATUS {
  CHAN_STATUS_CREATED,
  CHAN_STATUS_REQUESTED,
  CHAN_STATUS_OPEN,
  CHAN_STATUS_CLOSED,
};

struct SSH_CHAN {
  struct SSH_CONN *conn;
  void *userdata;
  enum CHAN_STATUS status;
  struct pollfd watch_fds[MAX_POLL_FDS];
  nfds_t num_watch_fds;

  uint32_t local_num;
  uint32_t remote_num;
  uint32_t local_max_packet_size;
  uint32_t local_window_size;
  uint32_t remote_max_packet_size;
  uint32_t remote_window_size;

  enum SSH_CHAN_TYPE type;
  void *type_config;
  ssh_chan_fn_open notify_open;
  ssh_chan_fn_open_failed notify_open_failed;
  ssh_chan_fn_closed notify_closed;
  ssh_chan_fn_fd_ready notify_fd_ready;
  ssh_chan_fn_received notify_received;
  ssh_chan_fn_received_ext notify_received_ext;
};

static const struct CHAN_TYPE_DATA {
  enum SSH_CHAN_TYPE type;
  const char *name;
} chan_types[] = {
  { SSH_CHAN_TYPE_SESSION, "session" },
};

static const struct CHAN_TYPE_DATA *chan_get_type_data(enum SSH_CHAN_TYPE type)
{
  int i;

  for (i = 0; i < sizeof(chan_types)/sizeof(chan_types[0]); i++)
    if (chan_types[i].type == type)
      return &chan_types[i];
  ssh_set_error("unknown channel type %d", type);
  return NULL;
}

static short chan_flags_to_pollfd_events(int chan_fd_flags)
{
  short events = 0;
  if ((chan_fd_flags & (SSH_CHAN_FD_READ|SSH_CHAN_FD_CLOSE)) != 0)
    events |= POLLIN | POLLHUP;
  if ((chan_fd_flags & SSH_CHAN_FD_WRITE) != 0)
    events |= POLLOUT;
  return events;
}

static int pollfd_events_to_chan_flags(short pollfd_events)
{
  int flags = 0;
  
  if ((pollfd_events & (POLLIN|POLLPRI)) != 0)
    flags |= SSH_CHAN_FD_READ;
  if ((pollfd_events & POLLHUP) != 0)
    flags |= SSH_CHAN_FD_CLOSE;
  if ((pollfd_events & (POLLOUT|POLLWRBAND)) != 0)
    flags |= SSH_CHAN_FD_WRITE;
  return flags;
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
  chan->userdata = cfg->userdata;
  chan->status = CHAN_STATUS_REQUESTED;
  chan->num_watch_fds = 0;
  
  chan->local_num = local_num;
  chan->remote_num = 0;
  chan->local_max_packet_size = 65536;
  chan->local_window_size = 256*1024;
  chan->remote_max_packet_size = 0;
  chan->remote_window_size = 0;

  chan->type = cfg->type;
  chan->type_config = cfg->type_config;
  chan->notify_open = cfg->notify_open;
  chan->notify_open_failed = cfg->notify_open_failed;
  chan->notify_closed = cfg->notify_closed;
  chan->notify_fd_ready = cfg->notify_fd_ready;
  chan->notify_received = cfg->notify_received;
  chan->notify_received_ext = cfg->notify_received_ext;
  
  conn->channels[conn->num_channels++] = chan;
  return chan;
}

void ssh_chan_free(struct SSH_CHAN *chan)
{
  ssh_free(chan);
}

static struct SSH_CHAN *chan_get_by_num(struct SSH_CONN *conn, uint32_t local_num)
{
  int i;

  for (i = 0; i < conn->num_channels; i++)
    if (conn->channels[i]->local_num == local_num)
      return conn->channels[i];
  ssh_set_error("unknown channel number '%u'\n", local_num);
  return NULL;
}

uint32_t ssh_chan_get_num(struct SSH_CHAN  *chan)
{
  return chan->local_num;
}

void ssh_chan_close(struct SSH_CHAN  *chan)
{
  if (chan->status == CHAN_STATUS_OPEN) {
    chan->notify_closed(chan, chan->userdata);
    chan->status = CHAN_STATUS_CLOSED;
  }
}

static void chan_remove_closed_channels(struct SSH_CONN *conn)
{
  int i;

  for (i = 0; i < conn->num_channels; ) {
    if (conn->channels[i]->status == CHAN_STATUS_CLOSED) {
      struct SSH_CHAN *free_chan = conn->channels[i];
      memmove(&conn->channels[i], &conn->channels[i+1], (conn->num_channels-i-1) * sizeof(struct SSH_CHANNEL *));
      conn->num_channels--;
      ssh_chan_free(free_chan);
    } else
      i++;
  }
}

int ssh_chan_send(struct SSH_CHAN *chan, void *data, size_t data_len)
{
  ssh_log("WARNING: ssh_chan_send() not implemented!\n");
  dump_mem("data to send", data, data_len);
  return 0;
}

int ssh_chan_send_ext(struct SSH_CHAN *chan, uint32_t data_type_code, void *data, size_t data_len)
{
  ssh_log("WARNING: ssh_chan_send_ext() not implemented!\n");
  dump_mem("ext data to send", data, data_len);
  return 0;
}

int ssh_chan_watch_fd(struct SSH_CHAN  *chan, int fd, uint8_t enable_fd_flags, uint8_t disable_fd_flags)
{
  short enable_events = chan_flags_to_pollfd_events(enable_fd_flags);
  short disable_events = chan_flags_to_pollfd_events(disable_fd_flags);
  int i;

  //ssh_log("watch fd %d with events (%d,%d)\n", fd, enable_fd_flags, disable_fd_flags);
  
  if (update_poll_fd_events(chan->watch_fds, &chan->num_watch_fds, fd, enable_events, disable_events) < 0) {
    if (enable_events != 0) // no error if there's no space to add only disable_events
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

static void chan_notify_channels_watch_fds(struct SSH_CONN *conn, struct pollfd *poll_fd)
{
  int i, j;

  for (i = 0; i < conn->num_channels; i++) {
    struct SSH_CHAN *chan = conn->channels[i];
    for (j = 0; j < chan->num_watch_fds; j++) {
      if (poll_fd->revents != 0 && poll_fd->fd == chan->watch_fds[j].fd) {
        chan->notify_fd_ready(chan, chan->userdata, poll_fd->fd,
                              pollfd_events_to_chan_flags(poll_fd->revents));
      }
    }
  }
}

static int chan_handle_global_request(struct SSH_CONN *conn, struct SSH_BUF_READER *pack)
{
  struct SSH_STRING req_name;
  uint8_t want_reply;

  if (ssh_buf_read_skip(pack, 1) < 0  // packet type
      || ssh_buf_read_string(pack, &req_name) < 0
      || ssh_buf_read_u8(pack, &want_reply) < 0)
    return -1;

  ssh_log("* received global request '%.*s' (want_reply=%d)\n", (int) req_name.len, req_name.str, want_reply);
  if (want_reply) {
    struct SSH_BUFFER *reply = ssh_conn_new_packet(conn);
    if (reply == NULL
        || ssh_buf_write_u8(reply, SSH_MSG_REQUEST_FAILURE) < 0
        || ssh_conn_send_packet(conn) < 0)
      return -1;
  }
  return 0;
}

static int chan_send_channel_open(struct SSH_CONN *conn, struct SSH_CHAN *chan)
{
  struct SSH_BUFFER *pack;
  const struct CHAN_TYPE_DATA *type_data = chan_get_type_data(chan->type);

  if (type_data == NULL
      || (pack = ssh_conn_new_packet(conn)) == NULL
      || ssh_buf_write_u8(pack, SSH_MSG_CHANNEL_OPEN) < 0
      || ssh_buf_write_cstring(pack, type_data->name) < 0
      || ssh_buf_write_u32(pack, chan->local_num) < 0
      || ssh_buf_write_u32(pack, chan->local_window_size) < 0
      || ssh_buf_write_u32(pack, chan->local_max_packet_size) < 0
      || ssh_conn_send_packet(conn) < 0)
    return -1;
  return 0;
}

static int chan_process_channel_packet(struct SSH_CONN *conn, struct SSH_BUF_READER *pack)
{
  struct SSH_CHAN *chan;
  uint8_t pack_type;
  uint32_t local_num;
  
  if (ssh_buf_read_u8(pack, &pack_type) < 0
      || ssh_buf_read_u32(pack, &local_num) < 0
      || (chan = chan_get_by_num(conn, local_num)) == NULL)
    return -1;

  switch (pack_type) {
  case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
    if (ssh_buf_read_u32(pack, &chan->remote_num) < 0
        || ssh_buf_read_u32(pack, &chan->remote_window_size) < 0
        || ssh_buf_read_u32(pack, &chan->remote_max_packet_size) < 0)
      return -1;
    switch (chan->type) {
    case SSH_CHAN_TYPE_SESSION:
      {
        struct SSH_CHAN_SESSION_CONFIG *cfg = chan->type_config;
        struct SSH_BUFFER *wpack;

        // TODO: check cfg to see if the user really wants a pty
        if ((wpack = ssh_conn_new_packet(conn)) == NULL
            || ssh_buf_write_u8(wpack, SSH_MSG_CHANNEL_REQUEST) < 0
            || ssh_buf_write_u32(wpack, chan->remote_num) < 0
            || ssh_buf_write_cstring(wpack, "pty-req") < 0
            || ssh_buf_write_u8(wpack, 0) < 0
            || ssh_buf_write_cstring(wpack, cfg->term) < 0
            || ssh_buf_write_u32(wpack, cfg->term_width) < 0
            || ssh_buf_write_u32(wpack, cfg->term_height) < 0
            || ssh_buf_write_u32(wpack, 0) < 0   // width pixels
            || ssh_buf_write_u32(wpack, 0) < 0   // height pixels
            || ssh_buf_write_cstring(wpack, "") < 0
            || ssh_conn_send_packet(conn) < 0)
          return -1;

        // TODO: run command instead of shell if cfg->command is set
        if ((wpack = ssh_conn_new_packet(conn)) == NULL
            || ssh_buf_write_u8(wpack, SSH_MSG_CHANNEL_REQUEST) < 0
            || ssh_buf_write_u32(wpack, chan->remote_num) < 0
            || ssh_buf_write_cstring(wpack, "shell") < 0
            || ssh_buf_write_u8(wpack, 1) < 0
            || ssh_conn_send_packet(conn) < 0)
          return -1;
      }
      break;
    default:
      ssh_log("WARNING: ignoring unknown channel type %d\n", chan->type);
      break;
    }
    break;
    
  case SSH_MSG_CHANNEL_OPEN_FAILURE:
    chan->notify_open_failed(chan, chan->userdata);
    break;

  case SSH_MSG_CHANNEL_SUCCESS:
    chan->status = CHAN_STATUS_OPEN;
    if (chan->notify_open(chan, chan->userdata) < 0)
      ssh_chan_close(chan);
    break;

  case SSH_MSG_CHANNEL_DATA:
    {
      struct SSH_STRING data;
      if (ssh_buf_read_string(pack, &data) < 0)
        return -1;
      chan->notify_received(chan, chan->userdata, data.str, data.len);
    }
    break;
    
  default:
    dump_packet_reader("unhandled channel packet", pack, conn->in_stream.mac_len);
    break;
  }

  return 0;
}

static int chan_process_packets(struct SSH_CONN *conn)
{
  while (1) {
    struct SSH_BUF_READER *pack = ssh_conn_recv_packet(conn);
    if (pack == NULL) {
      if (errno == EWOULDBLOCK)
        return 0;
      return -1;
    }

    switch (ssh_packet_get_type(pack)) {
    case SSH_MSG_GLOBAL_REQUEST:
      if (chan_handle_global_request(conn, pack) < 0)
        return -1;
      break;

    case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
    case SSH_MSG_CHANNEL_OPEN_FAILURE:
    case SSH_MSG_CHANNEL_SUCCESS:
    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
    case SSH_MSG_CHANNEL_DATA:
      if (chan_process_channel_packet(conn, pack) < 0)
        return -1;
      break;
      
    default:
      dump_packet_reader("received unknown packet", pack, conn->in_stream.mac_len);
      break;
    }
  }
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

    //ssh_log("* polling %d fds\n", (int) num_poll_fds); for (i = 0; i < num_poll_fds; i++) ssh_log(" -> fd %d with flags %d\n", poll_fds[i].fd, poll_fds[i].events);
    if (poll(poll_fds, num_poll_fds, -1) < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    //ssh_log("* got poll result:\n"); for (i = 0; i < num_poll_fds; i++) ssh_log(" -> fd %d has flags %d\n", poll_fds[i].fd, poll_fds[i].revents);

    if ((poll_fds[0].revents & POLLIN) != 0) {
      if (chan_process_packets(conn) < 0)
        return -1;
    }
    if ((poll_fds[0].revents & POLLOUT) != 0) {
      if (ssh_conn_send_flush(conn) < 0 && errno != EWOULDBLOCK)
        return -1;
    }

    for (i = 1; i < num_poll_fds; i++)
      chan_notify_channels_watch_fds(conn, &poll_fds[i]);
  }

  return 0;
}

static void chan_close_all_channels(struct SSH_CONN *conn)
{
  int i;
  
  for (i = 0; i < conn->num_channels; i++)
    ssh_chan_close(conn->channels[i]);
  chan_remove_closed_channels(conn);
}

int ssh_chan_run_connection(struct SSH_CONN *conn, int num_channels, const struct SSH_CHAN_CONFIG *channel_cfgs)
{
  int i;

  if (ssh_net_set_sock_blocking(conn->sock, 0) < 0)
    return -1;
  
  for (i = 0; i < num_channels; i++) {
    struct SSH_CHAN *chan = chan_new(conn, &channel_cfgs[i]);
    if (chan == NULL)
      return -1;
    if (chan_send_channel_open(conn, chan) < 0)
      return -1;
    chan->status = CHAN_STATUS_REQUESTED;
  }
  
  if (chan_loop(conn) < 0) {
    chan_close_all_channels(conn);
    return -1;
  }

  chan_close_all_channels(conn);
  return 0;
}
