/* packet.c */

#include <stdlib.h>
#include <string.h>

#include "common/buffer.h"

#include "common/error.h"
#include "common/alloc.h"

#define BUFFER_GROW_SIZE 256

/* check if 'a + b' doesn't overflow, if ok store the result in *ret */
static int checked_add(size_t *ret, size_t a, size_t b)
{
  if (a + b < a) {
    ssh_set_error("buffer size too large");
    return -1;
  }
  *ret = a + b;
  return 0;
}

/* check if 'pos+adv <= len' and the addition doesn't overflow */
static int check_advance(size_t pos, size_t adv, size_t len)
{
  size_t new_pos;
  
  if (checked_add(&new_pos, pos, adv) < 0) {
    ssh_set_error("data too large to read");
    return -1;
  }
  if (new_pos > len) {
    ssh_set_error("read past end of buffer");
    return -1;
  }
  return 0;
}

uint32_t ssh_buf_get_u32(uint8_t *data)
{
  return ((  (uint32_t) data[0] << 24)
          | ((uint32_t) data[1] << 16)
          | ((uint32_t) data[2] <<  8)
          | ((uint32_t) data[3]));
}

void ssh_buf_set_u32(uint8_t *data, uint32_t v)
{
  *data++ = v >> 24;
  *data++ = v >> 16;
  *data++ = v >> 8;
  *data++ = v;
}

/* --------------------------------------------------------------------- */
/* -- string ----------------------------------------------------------- */
/* --------------------------------------------------------------------- */

struct SSH_STRING ssh_str_new_from_buffer(struct SSH_BUFFER *buf)
{
  struct SSH_STRING str = {
    .str = buf->data,
    .len = buf->len
  };
  return str;
}

struct SSH_STRING ssh_str_new(uint8_t *data, size_t len)
{
  struct SSH_STRING str = {
    .str = data,
    .len = len
  };
  return str;
}

struct SSH_STRING ssh_str_new_empty(void)
{
  struct SSH_STRING str = {
    .str = NULL,
    .len = 0
  };
  return str;
}

int ssh_str_alloc(struct SSH_STRING *new_str, size_t len)
{
  new_str->len = len;
  new_str->str = ssh_alloc(len);
  if (new_str->str == NULL)
    return -1;
  return 0;
}

int ssh_str_dup_cstring(struct SSH_STRING *new_str, const char *str)
{
  size_t len = strlen(str);
  size_t len_plus_1;

  if (checked_add(&len_plus_1, len, 1) < 0
      || ssh_str_alloc(new_str, len_plus_1) < 0)
    return -1;
  memcpy(new_str->str, str, len);
  new_str->str[len] = '\0';
  new_str->len = len;
  return 0;
}

int ssh_str_dup_string(struct SSH_STRING *new_str, const struct SSH_STRING *str)
{
  if (ssh_str_alloc(new_str, str->len) < 0)
    return -1;
  memcpy(new_str->str, str->str, str->len);
  return 0;
}

int ssh_str_cmp_data(const struct SSH_STRING *str, const uint8_t *data, size_t data_len)
{
  size_t cmp_len = (str->len < data_len) ? str->len : data_len;
  int ret = memcmp(str->str, data, cmp_len);
  if (ret != 0)
    return ret;
  if (str->len < data_len)
    return -1;
  if (str->len > data_len)
    return 1;
  return 0;
}

int ssh_str_cmp_string(const struct SSH_STRING *str1, const struct SSH_STRING *str2)
{
  return ssh_str_cmp_data(str1, str2->str, str2->len);
}

int ssh_str_cmp_cstring(const struct SSH_STRING *str1, const char *str2)
{
  return ssh_str_cmp_data(str1, (uint8_t *) str2, strlen(str2));
}

void ssh_str_free(struct SSH_STRING *str)
{
  if (str->str != NULL && str->len > 0)
    memset(str->str, 0, str->len);
  ssh_free(str->str);
  str->str = NULL;
  str->len = 0;
}

/* --------------------------------------------------------------------- */
/* -- buffer ----------------------------------------------------------- */
/* --------------------------------------------------------------------- */

struct SSH_BUFFER ssh_buf_new(void)
{
  struct SSH_BUFFER ret = {
    .data = NULL,
    .len = 0,
    .cap = 0
  };
  return ret;
}

void ssh_buf_free(struct SSH_BUFFER *buf)
{
  ssh_free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

struct SSH_BUFFER ssh_buf_new_from_data(uint8_t *data, size_t len)
{
  struct SSH_BUFFER ret = {
    .data = data,
    .len = len,
    .cap = len
  };
  return ret;
}

int ssh_buf_ensure_size(struct SSH_BUFFER *buf, size_t new_len)
{
  if (buf->cap >= new_len)
    return 0;

  return ssh_buf_grow(buf, new_len - buf->len);
}

int ssh_buf_grow(struct SSH_BUFFER *buf, size_t add_len)
{
  void *new_data;
  size_t new_cap;

  /* get desired length */
  if (checked_add(&new_cap, buf->len, add_len) < 0)
    return -1;
  if (buf->cap >= new_cap)
    return 0;

  /* round length up */
  if (checked_add(&new_cap, new_cap, BUFFER_GROW_SIZE+1) < 0)
    return -1;
  new_cap = new_cap / BUFFER_GROW_SIZE * BUFFER_GROW_SIZE;

  /* grow to new length */
  new_data = ssh_realloc(buf->data, new_cap);
  if (new_data == NULL)
    return -1;

  buf->data = new_data;
  buf->cap = new_cap;
  return 0;
}

void ssh_buf_clear(struct SSH_BUFFER *buf)
{
  buf->len = 0;
}

uint8_t *ssh_buf_get_write_pointer(struct SSH_BUFFER *buf, size_t len)
{
  uint8_t *ret;
  
  if (ssh_buf_grow(buf, len) < 0)
    return NULL;
  ret = buf->data + buf->len;
  buf->len += len;
  return ret;
}

int ssh_buf_write_u8(struct SSH_BUFFER *buf, uint8_t val)
{
  if (ssh_buf_grow(buf, 1) < 0)
    return -1;
  buf->data[buf->len++] = val;
  return 0;
}

int ssh_buf_write_u32(struct SSH_BUFFER *buf, uint32_t val)
{
  if (ssh_buf_grow(buf, 4) < 0)
    return -1;
  buf->data[buf->len++] = val >> 24;
  buf->data[buf->len++] = val >> 16;
  buf->data[buf->len++] = val >> 8;
  buf->data[buf->len++] = val;
  return 0;
}

int ssh_buf_write_data(struct SSH_BUFFER *buf, const uint8_t *val, size_t len)
{
  uint8_t *p;

  if (ssh_buf_write_u32(buf, len) < 0)
    return -1;
  
  p = ssh_buf_get_write_pointer(buf, len);
  if (p == NULL)
    return -1;

  memcpy(p, val, len);
  return 0;
}

int ssh_buf_write_cstring(struct SSH_BUFFER *buf, const char *val)
{
  return ssh_buf_write_data(buf, (uint8_t *) val, strlen(val));
}

int ssh_buf_write_string(struct SSH_BUFFER *buf, const struct SSH_STRING *val)
{
  return ssh_buf_write_data(buf, val->str, val->len);
}

int ssh_buf_write_buf_reader(struct SSH_BUFFER *buf, const struct SSH_BUF_READER *val)
{
  return ssh_buf_write_data(buf, val->data, val->len);
}

int ssh_buf_write_buffer(struct SSH_BUFFER *buf, const struct SSH_BUFFER *val)
{
  return ssh_buf_write_data(buf, val->data, val->len);
}

int ssh_buf_append_data(struct SSH_BUFFER *buf, const uint8_t *data, size_t len)
{
  uint8_t *p;
  
  p = ssh_buf_get_write_pointer(buf, len);
  if (p == NULL)
    return -1;
  memcpy(p, data, len);

  return 0;
}

int ssh_buf_append_cstring(struct SSH_BUFFER *buf, const char *val)
{
  return ssh_buf_append_data(buf, (uint8_t *) val, strlen(val));
}

int ssh_buf_append_string(struct SSH_BUFFER *buf, const struct SSH_STRING *val)
{
  return ssh_buf_append_data(buf, val->str, val->len);
}

int ssh_buf_append_buffer(struct SSH_BUFFER *buf, const struct SSH_BUFFER *val)
{
  return ssh_buf_append_data(buf, val->data, val->len);
}

int ssh_buf_append_buf_reader(struct SSH_BUFFER *buf, const struct SSH_BUF_READER *val)
{
  return ssh_buf_append_data(buf, val->data, val->len);
}

int ssh_buf_remove_data(struct SSH_BUFFER *buf, size_t offset, size_t len)
{
  size_t end;
  
  if (checked_add(&end, offset, len) < 0
      || buf->len < end) {
    ssh_set_error("trying to remove data outside buffer");
    return -1;
  }
  if (buf->len > end)
    memmove(buf->data + offset, buf->data + end, buf->len - end);
  buf->len -= len;
  return 0;
}

/* --------------------------------------------------------------------- */
/* -- buf_reader ------------------------------------------------------- */
/* --------------------------------------------------------------------- */

struct SSH_BUF_READER ssh_buf_reader_new(uint8_t *data, size_t len)
{
  struct SSH_BUF_READER ret = {
    .data = data,
    .pos = 0,
    .len = len
  };
  return ret;
}

struct SSH_BUF_READER ssh_buf_reader_new_from_buffer(struct SSH_BUFFER *buf)
{
  struct SSH_BUF_READER ret = {
    .data = buf->data,
    .pos = 0,
    .len = buf->len
  };
  return ret;
}

struct SSH_BUF_READER ssh_buf_reader_new_from_string(struct SSH_STRING *str)
  {
  struct SSH_BUF_READER ret = {
    .data = str->str,
    .pos = 0,
    .len = str->len
  };
  return ret;
}

void ssh_buf_reader_rewind(struct SSH_BUF_READER *buf)
{
  buf->pos = 0;
}

int ssh_buf_reader_seek(struct SSH_BUF_READER *buf, size_t new_pos)
{
  if (new_pos > buf->len) {
    ssh_set_error("seek to invalid position");
    return -1;
  }
  buf->pos = new_pos;
  return 0;
}

int ssh_buf_read_u8(struct SSH_BUF_READER *buf, uint8_t *ret_val)
{
  if (check_advance(buf->pos, 1, buf->len) < 0)
    return -1;

  if (ret_val != NULL)
    *ret_val = buf->data[buf->pos];
  buf->pos++;
  return 0;
}

int ssh_buf_read_u32(struct SSH_BUF_READER *buf, uint32_t *ret_val)
{
  if (check_advance(buf->pos, 4, buf->len))
    return -1;

  if (ret_val != NULL)
    *ret_val = ((  (uint32_t) buf->data[buf->pos  ] << 24)
                | ((uint32_t) buf->data[buf->pos+1] << 16)
                | ((uint32_t) buf->data[buf->pos+2] << 8)
                | ((uint32_t) buf->data[buf->pos+3]));
  buf->pos += 4;
  return 0;
}

int ssh_buf_read_string(struct SSH_BUF_READER *buf, struct SSH_STRING *ret_val)
{
  uint32_t len;

  if (ssh_buf_read_u32(buf, &len) < 0)
    return -1;
  
  if (check_advance(buf->pos, len, buf->len) < 0)
    return -1;

  if (ret_val != NULL) {
    ret_val->str = buf->data + buf->pos;
    ret_val->len = len;
  }
  buf->pos += len;
  return 0;
}

int ssh_buf_read_until(struct SSH_BUF_READER *buf, uint8_t sentinel, struct SSH_STRING *ret_val)
{
  size_t start_pos = buf->pos;

  while ((buf->pos < buf->len) && (buf->data[buf->pos] != sentinel))
    buf->pos++;
  if (ret_val != NULL) {
    ret_val->str = buf->data + start_pos;
    ret_val->len = buf->pos - start_pos;
  }
  if (buf->pos < buf->len)
    buf->pos++;  // skip sentinel (but don't include it)
  return 0;
}

int ssh_buf_read_skip(struct SSH_BUF_READER *buf, size_t len)
{
  if (check_advance(buf->pos, len, buf->len) < 0)
    return -1;

  buf->pos += len;
  return 0;
}
