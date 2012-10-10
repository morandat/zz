#ifndef _Z2Z_CONNECT_H_
#define _Z2Z_CONNECT_H_

#include <netinet/in.h> // for struct sockaddr
#include <pthread.h> //pthread_create()
#include <stdlib.h> //for type size_t
#include "z2z_lock.h"

#define CONN_INITBUFSIZE	1024

enum gw_conn_state
{
  CONN_CLOSE = 0,
  CONN_OPEN = 1,
  CONN_UNKNOWN = -2,
  CONN_READ_ERROR = 3,
  CONN_WRITE_ERROR = 4,
  CONN_EOF = 5
};
typedef enum gw_conn_state CONN_STATE;

static char* conn_state_table[] =
{ "CONN_closed", "CONN_open", "CONN_unknown", "CONN_read_error",
    "CONN_write_error" };

struct gw_conn
{
  int sock;
  int flags;
  int status; // status of the connection

  struct sockaddr_storage *local_addr;
  socklen_t local_addr_len;
  struct sockaddr_storage *remote_addr;
  socklen_t remote_addr_len;

  socklen_t len;

  void *rbuf; //read buffer
  size_t rbuflen; //Total size of the buffer
  int rbuflenused; //Number of bytes read
  void *rbuf_ptr; //Pointer on the read buffer

  void *wbuf; //write buffer
  size_t wbuflen;
  int wbuflenused; //pos to add new data
  void *wbuf_ptr; //pointer on the write buffer

  // statistics
  size_t bytes_out; //total of bytes read, as we can perform several times a read on the sock.
  size_t bytes_in;
  int msg_out;
  int msg_in;

  //protect structure
  pthread_mutex_t lock; //Lock the structure as a whole
  pthread_mutex_t rlock; //Lock the fields related to the buffer used to read data
  pthread_mutex_t wlock; //Lock the fields related to the buffer used to write data
  pthread_mutex_t ilock; //Lock the fields related to the stats
  pthread_mutex_t slock; //Lock the fields related to the status
  pthread_mutex_t alock; //Lock the fields related to the address

  //control the structure for tcp stream if needed
  Z2Z_LOCK_PTR z2z_lock;

  //receive handler
  void* (*recv_hdlr)(void *);
};

typedef struct gw_conn gw_conn_t;
typedef gw_conn_t* GW_CONN_PTR;

<<<<<<< .mine
   /**
    * Extend the internal receive buffer of a connection <code>conn</code> 
    *
    * @param conn, a pointer to a connection structure 
    * @param newsize, the newsize of the internal buffer
    * @return <code>EXIT_SUCCESS</code> 
    *        or <code>EXIT_FAILURE</code> if an error occured.
    */
   extern int gw_conn_grow_rbuf (GW_CONN_PTR conn, size_t newsize);
   extern int gw_conn_reset_rbuf(GW_CONN_PTR conn,Z2Z_MTL_LOCK_PTR lock);
   extern GW_CONN_PTR gw_create_new_client_tcp_connection(const char * remote_host, int remote_port);
=======
/**
 * Extend the internal receive buffer of a connection <code>conn</code>
 *
 * @param conn, a pointer to a connection structure
 * @param newsize, the newsize of the internal buffer
 * @return <code>EXIT_SUCCESS</code>
 *        or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_grow_rbuf(GW_CONN_PTR conn, size_t newsize);
extern int gw_conn_reset_rbuf(GW_CONN_PTR conn);
extern GW_CONN_PTR gw_create_new_client_tcp_connection(
    const char * remote_host, int remote_port);
>>>>>>> .r4986

// Required by the smtp client
extern void gw_set_client_socket_ops(GW_CONN_PTR conn);

extern void unlock_conn(GW_CONN_PTR conn);
extern void lock_conn(GW_CONN_PTR conn);

extern GW_CONN_PTR gw_create_new_client_udp_connection(
    const char * remote_host, int remote_port);

/**
 * Extend the internal output buffer of a connection <code>conn</code>
 *
 * @param conn, a pointer to a connection structure
 * @param newsize, the newsize of the internal buffer
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_grow_wbuf(GW_CONN_PTR conn, size_t newsize);

/**
 * Add <code>len</code> char from buffer <code>buff</code> to the connection output buffer
 *
 * @param conn, a pointer to a connection structure
 * @param buff, buffer containing data to add
 * @param len, size of the buffer <code>buff</code> to add
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_add_to_wbuf(GW_CONN_PTR conn, const char* buff, size_t len);

/**
 * Send the internal output buffer encapsulated int the connection <code>conn<code>
 * * @param conn, a pointer to a connection structure
 * @return number of bytes sent
 */
extern int gw_conn_send_wbuf(GW_CONN_PTR conn);

/**
 * Send <code>size</code> bytes from <code>data</code> on the connection <code>conn</code>
 * @param conn, a pointer to a connection structure
 * @param size, number of bytes to send
 * @return number of bytes sent
 */
extern int gw_conn_send_without_buffering(GW_CONN_PTR conn, char* data,
    size_t size);

/**
 * Free an allocated connection structure
 *
 *@param conn the connection structure to free
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_free(GW_CONN_PTR conn);

/**
 * Handle a new connection on a udp socket and call <code>gw_recv_func</code> to receive data
 *
 * @param fd, a file descriptor corresponding to a udp socket
 * @param gw_recv_func, a function
 *                like <code>gw_conn_udp_recv</code> to receive data from the opened socket,
 * @return an allocated connection on the open udp socket
 */
extern GW_CONN_PTR gw_handle_new_udp_connection(int fd, void* (*gw_recv_func)(
    void*));

/**
 * Handle a new connection on a tcp socket and call <code>gw_recv_func</code> to receive data
 * WARNING, the call is blocked until the tcp socket is closed
 *
 * @param fd, a file descriptor corresponding to a tcp socket
 * @param gw_recv_func, a function
 *                like <code>gw_conn_tcp_recv</code> to receive data from the opened socket,
 * @return an allocated connection on the open tcp socket,
 *         or <code>NULL</code> if an error occured.
 */
extern GW_CONN_PTR gw_handle_new_tcp_connection(int fd, void* (*gw_recv_func)(
    void*));

/**
 * Receive bytes from a given connection using an udp connection
 *
 *@param conn, an allocated connection with a udp opened socket
 *@return int, the number of bytes read, or <code>EXIT_FAILURE</code> if an error occured.
 */
extern void* gw_conn_udp_recv(void * arg);

/**
 * Receive bytes from a given connection using a tcp connection
 * WARNING, the call is blocked until the tcp socket is closed
 *
 *@param conn, an allocated connection with a tcp opened socket
 *@return int, the number of bytes read, <code>EXIT_FAILURE</code> otherwise
 */
extern void* gw_conn_tcp_recv(void * arg);

/**
 * Receive bytes from a given connection using either an udp or tcp connection
 *
 * @param conn, an allocated connection with an opened socket
 * @return int, the number of bytes read, <code>EXIT_FAILURE</code> otherwise
 */
extern void* gw_conn_recv(void * arg);

/**
 * Close the socket used the given connection
 *
 * @param conn, an allocated connection with an opened socket
 * @return  <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_close(GW_CONN_PTR conn);

/**
 * Allocate a new gateway connection structure and return it.
 *
 * @param sock, A socket previously open
 * @return GW_CONN_PTR, A pointer to a connection structure
 *          or <code>NULL</code> if an error occured.
 */
extern GW_CONN_PTR gw_conn_alloc(int sock);

<<<<<<< .mine
   /**
    * Handle a new connection from a socket which may be either tcp or udp.
    * This function is a merge of the corresponding functions : 
    * gw_handle_new_tcp_connection and gw_handle_new_udp_connection.
    * the default function called gw_conn_recv is internally called to receive data from the socket
    *
    * @param fd, A file descriptor corresponding to an open socket
    * @return gw_conn_t*, A pointer to a connection stucture with input buffer filled
    */
   extern GW_CONN_PTR gw_handle_new_connection(int fd);
   extern char* gw_conn_get_received_msg(GW_CONN_PTR conn);
   extern int gw_conn_update_ref(GW_CONN_PTR conn);
   extern int gw_tcp_read_line(GW_CONN_PTR conn, char*** line, size_t size, size_t *len);
=======
/**
 * Handle a new connection from a socket which may be either tcp or udp.
 * This function is a merge of the corresponding functions :
 * gw_handle_new_tcp_connection and gw_handle_new_udp_connection.
 * the default function called gw_conn_recv is internally called to receive data from the socket
 *
 * @param fd, A file descriptor corresponding to an open socket
 * @return gw_conn_t*, A pointer to a connection stucture with input buffer filled
 */
extern GW_CONN_PTR gw_handle_new_connection(int fd);
extern char* gw_conn_get_received_msg(GW_CONN_PTR conn);
extern int gw_conn_update_ref(GW_CONN_PTR conn);
extern int gw_tcp_read_line(GW_CONN_PTR conn, char* line, size_t size,
    size_t *len);
>>>>>>> .r4986

/**
 *
 *
 */
extern GW_CONN_PTR gw_new_tcp_connection(int fd);
extern int gw_conn_nb_rbytes(GW_CONN_PTR conn);
extern int gw_conn_get_status(GW_CONN_PTR conn);
extern void gw_conn_set_status(GW_CONN_PTR conn, int val);
extern int gw_tcp_read(GW_CONN_PTR conn, size_t len);
extern int gw_conn_tcp_read(GW_CONN_PTR conn, size_t len);
extern int gw_conn_tcp_fill_buffer(GW_CONN_PTR conn);
extern int gw_conn_get_sock_type(GW_CONN_PTR conn);

#endif // _Z2Z_CONNECT_H_
