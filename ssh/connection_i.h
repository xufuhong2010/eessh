/* connection_i.h */

#ifndef CONNECTION_I_H_FILE
#define CONNECTION_I_H_FILE

#include "ssh/connection.h"
#include "crypto/algorithms.h"
#include "ssh/mac_i.h"
#include "ssh/stream_i.h"

struct SSH_STRING ssh_conn_get_server_hostname(struct SSH_CONN *conn);
struct SSH_STRING ssh_conn_get_username(struct SSH_CONN *conn);
ssh_conn_password_reader ssh_conn_get_password_reader(struct SSH_CONN *conn);

void ssh_conn_set_session_id(struct SSH_CONN *conn, struct SSH_STRING *session_id);
struct SSH_STRING *ssh_conn_get_session_id(struct SSH_CONN *conn);
int ssh_conn_set_cipher(struct SSH_CONN *conn, enum SSH_CONN_DIRECTION dir, enum SSH_CIPHER_TYPE type, struct SSH_STRING *iv, struct SSH_STRING *key);
int ssh_conn_set_mac(struct SSH_CONN *conn, enum SSH_CONN_DIRECTION dir, enum SSH_MAC_TYPE type, struct SSH_STRING *key);
int ssh_conn_check_server_identity(struct SSH_CONN *conn, struct SSH_STRING *server_host_key);

struct SSH_BUFFER *ssh_conn_new_packet(struct SSH_CONN *conn);
int ssh_conn_send_packet(struct SSH_CONN *conn);

struct SSH_BUF_READER *ssh_conn_recv_packet(struct SSH_CONN *conn);
struct SSH_BUF_READER *ssh_conn_recv_packet_skip_ignore(struct SSH_CONN *conn);

#endif /* CONNECTION_I_H_FILE */