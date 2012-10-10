#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "z2z_connection.h"
#include "z2z_socket.h"
#include "z2z_string.h"
#include "z2z_log.h"
#include <stdlib.h>     // for type size_t
#include <errno.h>      // for variable errno
#include <string.h>     // for method strerror()
#include <netinet/in.h> // for struct sockaddr_in 
#include <unistd.h>     // for method close()
#include <arpa/inet.h>  // for method inet_ntop()
#include <sys/types.h>  // for type uint8#include <assert.h>
#include <assert.h>
#include <netinet/tcp.h> // for TCP_NODELAY

extern void lock_conn(GW_CONN_PTR conn)
{
  _lock(&conn->lock);
}

extern void unlock_conn(GW_CONN_PTR conn)
{
  _unlock(&conn->lock);
}

extern int gw_conn_get_sock_type(GW_CONN_PTR conn)
{
  int type = 0;
  socklen_t size = (socklen_t) sizeof(int);
  if (getsockopt(conn->sock, SOL_SOCKET, SO_TYPE, (char*) &type, &size) < 0)
  {
    ERR("Failed to get the type of the socket connection: %s ", strerror(errno));
    ERR("sock : %d, status : %s",conn->sock,conn_state_table[conn->status]);
    exit(EXIT_FAILURE);
  }

  return type;
}

extern char* gw_conn_get_received_msg(GW_CONN_PTR conn)
{
  /*
   void* msg=NULL;
   lock_conn(conn);
   msg=malloc(sizeof(char)*(conn->rbuflenused));
   memcpy(msg,conn->rbuf,conn->rbuflenused);
   unlock_conn(conn);
   return msg;
   */
  return conn->rbuf;
}
/** * Extend the internal input buffer of a connection <code>conn</code> 
 *
 * @param conn, a pointer to a connection structure 
 * @param newsize, the newsize of the internal buffer
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_grow_rbuf(GW_CONN_PTR conn, size_t newsize)
{
  void *tmp;

  if ((tmp = realloc(conn->rbuf, newsize)) == NULL)
  {
    ERR("Failed to grow connection read buffer");
    return EXIT_FAILURE;
  }

  conn->rbuf = tmp;
  conn->rbuflen = newsize;

  return EXIT_SUCCESS;
}

/**
 * Extend the internal output buffer of a connection <code>conn</code> 
 *
 * @param conn, a pointer to a connection structure 
 * @param newsize, the newsize of the internal buffer
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_grow_wbuf(GW_CONN_PTR conn, size_t newsize)
{
  void *tmp;

  if ((tmp = realloc(conn->wbuf, newsize)) == NULL)
  {
    ERR("Failed to grow connection read buffer");
    return EXIT_FAILURE;
  }

  conn->wbuf = tmp;
  conn->wbuflen = newsize;

  return EXIT_SUCCESS;
}

/**
 * Add <code>len</code> char from buffer <code>buff</code> to the connection output buffer
 *
 * @param conn, a pointer to a connection structure 
 * @param buff, buffer containing data to add
 * @param len, size of the buffer <code>buff</code> to add
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_add_to_wbuf(GW_CONN_PTR conn, const char* buff, size_t len)
{
  int ret;
  size_t old_size, new_size;

  //  lock_conn(conn);

  old_size = conn->wbuflen;
  if (!conn->wbuf || ((conn->wbuflenused + len) > (conn->wbuflen - 1)))
  {
    new_size = (conn->wbuflenused + len) * 2;
    if ((ret = gw_conn_grow_wbuf(conn, new_size)) == EXIT_FAILURE)
      return EXIT_FAILURE;
    WARN("Socket on fd %d has overrun internal buffer - auto extend the buffer - old_size: %d new_size:%d \n",conn->sock,old_size,new_size);
  }

  memcpy(conn->wbuf + conn->wbuflenused, buff, len);
  conn->wbuflenused += len;

  // unlock_conn(conn);

  return EXIT_SUCCESS;
}

/**
 * Send <code>size</code> bytes from <code>data</code> on the connection <code>conn</code>
 * Perform an asynchronous send (on udp) if the socket used by the connection <code>conn</code> is unconnected (SOCK_DGRAM)
 * Or perform a sychronous send (on tcp) if the socket used by the connection <code>conn</code> is connected (SOCK_STREAM)
 * @param conn, a pointer to a connection structure
 * @param t, number of bytes to send 
 * @return number of bytes sent
 */
extern int gw_conn_send_without_buffering(GW_CONN_PTR conn, char* data,
    size_t t)
{
  int ret = 0;
  int type;

  _lock(&conn->wlock); //18/04/09

  //  if ((conn->status==CONN_CLOSE) ||(conn->status==CONN_UNKNOWN) ) {
  if ((gw_conn_get_status(conn) == CONN_CLOSE) || (gw_conn_get_status(conn)
      == CONN_UNKNOWN))
  {
    ERR("Socket %d is closed or its status is unknown, Unable to send data", conn->sock);
    _unlock(&conn->wlock); //18/04/09
    return EXIT_FAILURE;
  }

  if (data == NULL)
  {
    ERR("Attempt to send NULL data on Socket %d", conn->sock);
    _unlock(&conn->wlock); //18/04/09
    return EXIT_FAILURE;
  }

  if ((conn->wbuf != NULL) && (conn->wbuflenused > 0))
  {
    WARN("Send data without buffering whereas output buffer is not empty on Socket %d", conn->sock);
  }

  type = gw_conn_get_sock_type(conn);

  switch (type)
  {
  case SOCK_STREAM:
    //INFO("SOCK_STREAM");
    ret = sock_tcp_send(conn->sock, data, t, 0);
    if (ret < 0)
    {
      if ((errno == EPIPE) || (errno == ECONNRESET))
      {
        gw_conn_set_status(conn, CONN_CLOSE);
        ERR("Attempt to send data on closed socket #%d", conn->sock);
        _unlock(&conn->wlock); //18/04/09
        return EXIT_FAILURE;
      }
    }
    break;
  case SOCK_DGRAM:
    //INFO("SOCK_DGRAM");
    _lock(&conn->alock);
    ret = sock_udp_send(conn->sock, data, t, (SA*) conn->remote_addr,
        conn->remote_addr_len, 0);
    _unlock(&conn->alock);
    if (ret < 0)
    {
      if ((errno == EPIPE) || (errno == ECONNRESET))
      {
        gw_conn_set_status(conn, CONN_CLOSE);
        ERR("Attempt to send data on closed socket #%d", conn->sock);
        _unlock(&conn->wlock); //18/04/09
        return EXIT_FAILURE;
      }
    }
    break;
  default:
    ERR("Failed to get the type of the socket connection: %s ", strerror(errno));
    _unlock(&conn->wlock); //18/04/09
    return (EXIT_FAILURE);
  }

  _lock(&conn->ilock);
  conn->bytes_out += ret;
  conn->msg_out++;
  INFO(">>> sent %d bytes on %d, total msg sent %d",ret,t,conn->msg_out);
  _unlock(&conn->ilock);

  INFO(">>> sent %s",data);

  /*
   char* ptr=NULL;
   ptr = addSlashes(data, t);
   INFO(">>> sent %s",ptr);
   free(ptr);
   */

  _unlock(&conn->wlock); //18/04/09
  return EXIT_SUCCESS;
}

extern int gw_conn_reset_rbuf(GW_CONN_PTR conn)
{
  //lock_conn(conn);
  _lock(&conn->rlock); //18/04/09
  memset((char*) conn->rbuf, '\0', conn->rbuflen);
  conn->rbuflenused = 0;
  //unlock_conn(conn); 
  _unlock(&conn->rlock); //18/04/09
  return EXIT_SUCCESS;
}

/**
 * Free an allocated connection structure 
 *
 * @param conn the connection structure to free
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_free(GW_CONN_PTR conn)
{
  if (conn == NULL)
    return EXIT_SUCCESS;
  lock_conn(conn);
  if (conn->sock >= 0)
  {
    if (conn->status != CONN_CLOSE)
    {
      sock_close(conn->sock);
      INFO("sock_id: %d closed", conn->sock);
    }
else      ERR("sock_id: %d has already been closed for this thread", conn->sock);
    }
    if (conn->rbuf != NULL)
    free(conn->rbuf);
    if (conn->wbuf != NULL)
    free(conn->wbuf);
    if(conn->remote_addr !=NULL)
    free(conn->remote_addr);
    if(conn->local_addr !=NULL)
    free(conn->local_addr);
    if(conn->z2z_lock !=NULL)
    gw_destroy_tcp_lock(conn->z2z_lock);
    conn->rbuf_ptr=NULL;
    conn->wbuf_ptr=NULL;
    conn->remote_addr=NULL;
    conn->local_addr=NULL;
    unlock_conn(conn);

    if(&conn->lock!=NULL)
    _destroy_lock(&(conn->lock));
    if(&conn->rlock!=NULL)
    _destroy_lock(&(conn->rlock));
    if(&conn->wlock!=NULL)
    _destroy_lock(&(conn->wlock));
    if(&conn->ilock!=NULL)
    _destroy_lock(&(conn->ilock));
    if(&conn->slock!=NULL)
    _destroy_lock(&(conn->slock));
    if(&conn->alock!=NULL)
    _destroy_lock(&(conn->alock));

    free(conn);
    conn=NULL;

    return EXIT_SUCCESS;
  }

  /**
   *
   * Avoid to read directly in the connection buffer
   * Read in an external buffer from the given connection
   *
   *
   * @param len is a pointer that give the size of extbuf,
   * and return the number of char read
   */
  //extern void gw_tcp_recv(GW_CONN_PTR conn, char *extbuf, size_t *len)
  //{
  //  *len=(int) recv(conn->sock, extbuf, *len, 0)) == 0);
  //}

extern int gw_conn_tcp_read(GW_CONN_PTR conn, size_t len)
{
  //lock_conn(conn);
  _lock(&conn->rlock); //18/04/09
  again: if ((conn->rbuflenused = (int) sock_read(conn->sock, conn->rbuf, len,
      0)) < 0)
  {
    if (errno == EINTR)
    {
      goto again;
    }
    else if (errno == EAGAIN)
    {
      WARN("The socket is marked non-blocking and no pending connections are present on the queue: %s\n", strerror(errno));
    }
    ERR(" %s\n", strerror(errno));
    //unlock_conn(conn);
    _unlock(&conn->rlock); //18/04/09
    return (-1);
  }
  else if (conn->rbuflenused == 0)
  {
    //conn->status = CONN_EOF;
    gw_conn_set_status(conn, CONN_EOF);
    INFO("tcp_read: EOF");
    //unlock_conn(conn);
    _unlock(&conn->rlock); //18/04/09
    //    INFO("CONN_SOCK:%d CONN STATUS: %d -> %s",conn->sock,conn->status,strerror(errno));
    return 0;
  }

  //Update stat
  conn->msg_in++;
  conn->bytes_in += conn->rbuflenused;
  //return EXIT_SUCCESS;
  //unlock_conn(conn);
  _unlock(&conn->rlock); //18/04/09
  return conn->rbuflenused;
}

/**
 *
 *
 */
extern int gw_conn_tcp_buffered_read_byte(GW_CONN_PTR conn)
{
  if (conn->rbuf_ptr >= conn->rbuf + conn->rbuflenused)
  {
    if (gw_conn_tcp_fill_buffer(conn) < 0)
    {
      return -1;
    }
    conn->rbuf_ptr = conn->rbuf;
  }
  return *((int*) ((conn->rbuf_ptr)++));
}

/**
 * Fill the internal buffer int sock_read(int socket, void *buffer, int size, int timeout);
 *//*
 extern int gw_conn_tcp_fill_buffer(GW_CONN_PTR conn, int timeout)
 {
 ERR("gw_conn_tcp_fill_buffer starting");
 int n=0;
 conn->rbuflenused=0;

 while(conn->rbuflen > conn->rbuflenused) {
 n=sock_read(conn->sock,conn->rbuf+conn->rbuflenused,conn->rbuflen - conn->rbuflenused,timeout);
 timeout= 0;

 if(n > 0) {
 conn->rbuflenused+= n;
 continue;
 }

 if(n==0) break;

 if(n < 0) {
 if(errno == EINTR) continue;
 if(errno == EAGAIN || errno == EWOULDBLOCK) break;
 return -1;
 }
 }

 conn->bytes_in+=conn->rbuflenused;
 WARN("gw_conn_tcp_fill_buffer ending");
 return conn->rbuflenused;
 }*/

extern int gw_conn_tcp_fill_buffer(GW_CONN_PTR conn)
{
  ERR("gw_conn_tcp_fill_buffer starting");
  //lock_conn(conn);

  _lock(&conn->rlock); //18/04/09
  again: if ((conn->rbuflenused = sock_read(conn->sock, conn->rbuf,
      conn->rbuflen, 0)) < 0)
  {
    if (errno == EINTR)
    {
      goto again;
    }
    else if (errno == EAGAIN)
    {
      WARN("The socket is marked non-blocking and no pending connections are present on the queue: %s\n", strerror(errno));
    }
    gw_conn_set_status(conn, CONN_CLOSE);
    //conn->status = CONN_CLOSE;
    //unlock_conn(conn);

    _unlock(&conn->rlock); //18/04/09
    return (-1);
  }
  else if (conn->rbuflenused == 0)
  {
    //conn->status = CONN_EOF;
    INFO("tcp_read: EOF");
    gw_conn_set_status(conn, CONN_EOF);
    //unlock_conn(conn);

    _unlock(&conn->rlock); //18/04/09
    return 0;
  }

  _lock(&conn->ilock);//22/04/09
  conn->msg_in++;
  conn->bytes_in += conn->rbuflenused;
  _unlock(&conn->ilock);
  //unlock_conn(conn);

  _unlock(&conn->rlock); //18/04/09

  ERR("gw_conn_tcp_fill_buffer ending");
  return conn->rbuflenused;
}

/**
 * Wraps reads to provide a buffered read line
 * Result is given in *c
 *
 */
extern int gw_tcp_buffered_read_char(GW_CONN_PTR conn, char* c)
{
  if (conn->rbuflenused <= 0)
  {
    //INFO("Read from socket");

    again: if ((conn->rbuflenused = read(conn->sock, (uint8_t*) conn->rbuf,
        conn->rbuflen)) < 0)
    {
      if (errno == EINTR)
        goto again;
      return (-1);
    }
    else if (conn->rbuflenused == 0)
    {
      INFO("Nothing to read");
      return (0);
    }

    conn->rbuf_ptr = conn->rbuf;
  }

  conn->bytes_in += conn->rbuflenused;
  conn->rbuflenused--;

  assert(conn->rbuf_ptr!=NULL);

  *c = *((char*) ((conn->rbuf_ptr)++));
  return (conn->rbuflenused);
}

/**
 * conn => connection
 * line => line read
 * size => max line size
 * len  => nb bytes read in the line
 * return => EXIT_SUCESS or EXIT_FAILURE
 */
extern int gw_tcp_read_line(GW_CONN_PTR conn, char* line, size_t size,
    size_t *len)
{
  INFO("gw_tcp_read_line");
  int ret = 0;
  size_t i = 0;
  char c;

  while (i < size - 1)
  {
    if ((ret = gw_tcp_buffered_read_char(conn, &c)) >= 0)
    {
      line[i++] = c;
      if (c == '\n')
        break;
    }
    else
    {
      return EXIT_FAILURE;
    }
  }
  line[i] = '\0';
  *len = i;
  return EXIT_SUCCESS;
}

extern int gw_conn_get_status(GW_CONN_PTR conn)
{
  int status = -1;
  //lock_conn(conn); 
  //22/04/09
  _lock(&conn->slock);
  status = conn->status;
  //unlock_conn(conn);
  _unlock(&conn->slock);
  return status;
}

extern void gw_conn_set_status(GW_CONN_PTR conn, int val)
{
  //lock_conn(conn);
  //22/04/09
  _lock(&conn->slock);
  conn->status = val;
  _unlock(&conn->slock);
  //unlock_conn(connection);
}

/**
 * Handle a new connection on a udp socket and call gw_recv_func to receive data 
 *
 * @param fd, a file descriptor corresponding to a udp socket
 * @param gw_recv_func, a function like 'gw_conn_udp_recv' to receive data from the opened socket, 
 * @return an allocated connection on the open udp socket
 */
extern GW_CONN_PTR gw_handle_new_udp_connection(int fd, void* (*gw_recv_func)(
    void*))
{

  INFO("gw_handle_new_udp_connection");

  struct addrinfo *addr = NULL;
  int sockfd;

  // We have a new connection coming in! 
  GW_CONN_PTR conn = NULL;

  conn = gw_conn_alloc(fd);
  gw_conn_set_status(conn, CONN_OPEN); //22/04/09
  //conn->status=CONN_OPEN; //set status to open

  gw_recv_func(conn);

  if ((gw_conn_nb_rbytes(conn) == 0) || (gw_conn_get_status(conn)
      == CONN_READ_ERROR))
  {
    gw_conn_free(conn);
    return NULL;
  }

  //Make sense only if the fd is equal to server sockfd, which is not always the case !

  //==== Begin Section ==================================//
  // I DO NOT UNDERSTAND THIS CODE anymore 17/05/2010    //

  WARN("Need to test if the current sockfd is server sockfd");
  WARN("If so, we need to avoid the creation of an another sockfd");
  WARN("Currently always create a sock fd, Do not make sense if sockfd is created by gw_create_udp_connection");

  addr = sock_get_addrinfo(NULL, "0", SOCK_DGRAM, conn->remote_addr->ss_family);
  sockfd = sock_create_valid_socket(addr);
  conn->sock = sockfd;
  freeaddrinfo(addr);
  //==== End Section ====================================//
  INFO("gw_handle_new_udp_connection sock : %dEND",conn->sock);
  return conn;
}

/**
 * NEW FUNCTION TO TEST FOR THE NEW RUNTIME WITH TCP CONNECT FOR A STREAM
 * tcp connection from the left side (server side) 
 * Do not call anymore gw_recv_func to receive block data
 * It must be called in a dedicated thread in the request handler
 */
extern GW_CONN_PTR gw_handle_new_tcp_connection(int fd, void* (*gw_recv_func)(
    void *))
{

  INFO("gw_handle_new_tcp_connection: v2");

  socklen_t len;
  struct sockaddr_storage addr;

  // We have a new connection coming in! 
  GW_CONN_PTR conn = NULL;

  conn = gw_conn_alloc(-1);
  lock_conn(conn);
  len = sizeof(addr);

  if ((conn->sock = accept(fd, (SA *) &addr, &len)) == -1)
  {
    switch (errno)
    {
    case EAGAIN:
      WARN("The socket is marked non-blocking and no pending connections are present on the queue: %s\n", strerror(errno));
      gw_conn_free(conn);
      return (NULL);
      break;
    default:
      WARN("Failed to accept: %s\n", strerror(errno));
      gw_conn_free(conn);
      //return(NULL);
      exit(-1);
    }
  }
  conn->status = CONN_OPEN; //set status to open
  conn->remote_addr = malloc(len);
  memcpy(conn->remote_addr, &addr, len);
  conn->remote_addr_len = len;

  INFO("Connection status :%s",conn_state_table[conn->status]);
  INFO("Accepted tcp connection from %s, fd #%d.",sock_ntop((SA *)conn->remote_addr),fd);
  INFO("Client socket is : %d",conn->sock);
  unlock_conn(conn);
  return conn;
}

/**
 * Handle a new connection on a tcp socket and call gw_recv_func to receive data 
 * WARNING, the call is blocked until the tcp socket is closed
 * <---- 16/03/09 should not be used anymore 
 * keep it for downgrade compatibility
 * @param fd, a file descriptor corresponding to a tcp socket
 * @param gw_recv_func, a function like 'gw_conn_tcp_rev' to receive data from the opened socket, 
 * @return an allocated connection on the open tcp socket
 */

/*
 extern GW_CONN_PTR gw_handle_new_tcp_connection(int fd,void* (*gw_recv_func)(void *)) {

 INFO("gw_handle_new_tcp_connection");

 socklen_t size;

 size=sizeof(int);

 INFO("test_1");

 // We have a new connection coming in!
 GW_CONN_PTR conn=NULL;

 conn =gw_conn_alloc(-1);

 //BUGS HERE !!21 oct 2008


 if ((conn->sock = accept(fd, (SA *)&conn->remote_addr, &conn->remote_addr_len)) == -1) {
 switch(errno)
 {
 case EAGAIN:
 WARN("The socket is marked non-blocking and no pending connections are present on the queue: %s\n", strerror(errno));
 gw_conn_free(conn);
 return(NULL);
 break;
 default :
 WARN("Failed to accept: %s\n", strerror(errno));
 gw_conn_free(conn);
 return(NULL);
 }
 }else{
 conn->status=CONN_OPEN; //set status to open
 INFO("Connection status :%s",conn_state_table[conn->status]);
 INFO("Accepted tcp connection from %s, fd #%d.",sock_ntop((SA *)&conn->remote_addr),fd);
 }
 gw_recv_func(conn);
 return conn;
 }

 */

/**
 * Open a new connection to a remote tcp socket and call gw_recv_func to receive data 
 *
 * @param fd, a file descriptor corresponding to a tcp socket
 * @param gw_recv_func, a function like 'gw_conn_tcp_recv' to receive data from the opened socket, 
 * @param typet, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @return an allocated connection on the open tcp socket
 */
extern GW_CONN_PTR gw_create_new_client_tcp_connection(
    const char * remote_host, int remote_port)
{
  int sockfd;
  GW_CONN_PTR conn_;
  socklen_t len;
  //  INFO("gw_create_new_client_tcp_connection");

  assert(remote_host!=NULL);

  char serv[NI_MAXSERV];
  snprintf(serv, sizeof(serv), "%d", ntohs(remote_port));

  if ((sockfd = sock_init_tcp_connection(remote_host, remote_port, &len)) < 0)
  {
    WARN("Failed to open a connection to the host: %s, port:%i",remote_host,remote_port);
    return NULL;
  }
  conn_ = gw_conn_alloc(sockfd);
  lock_conn(conn_);
  conn_->len = len;
  conn_->status = CONN_OPEN;
  unlock_conn(conn_);
  // INFO("gw_create_new_client_tcp_connection ended");
  return conn_;
}

/*
 * Required by the smtp client.
 * Set TCP_NODELAY and SO_LINGER.
 *
 * What does SO_LINGER do? From http://developerweb.net/viewtopic.php?id=2982 :
 * Case 2: linger->l_onoff is non-zero and linger->l_linger is zero: A close() returns immediately.
 * The underlying stack discards any  unsent data, and, in the case of connection-oriented protocols
 * such as  TCP, sends a RST (reset) to the peer (this is termed a hard or abortive  close).
 * All subsequent attempts by the peer's application to  read()/recv() data will result in an ECONNRESET.
 */
extern void gw_set_client_socket_ops(GW_CONN_PTR conn)
{
  struct linger linger;
  int nodelay;

  // TCP NO DELAY
  nodelay = 1;
  if (setsockopt(conn->sock, IPPROTO_TCP, TCP_NODELAY, (char*) &nodelay,
      sizeof(int)) < 0)
  {
    fprintf(stderr, "setsockopt TCP_NODELAY %i", nodelay);
  }

  // SND BUFFER
//todo

  // RCV BUFFER
//todo

  // SO_LINGER
  linger.l_onoff = 1;
  linger.l_linger = 0;
  if (setsockopt(conn->sock, SOL_SOCKET, SO_LINGER, (char *) &linger,
      sizeof(linger)) < 0)
  {
    fprintf(stderr, "setsockopt SO_LINGER %d: %m", linger.l_linger);
  }
}

/**
 * Open a new connection to a remote tcp socket and call gw_recv_func to receive data 
 *
 * @param fd, a file descriptor corresponding to a tcp socket
 * @param gw_recv_func, a function like 'gw_conn_tcp_recv' to receive data from the opened socket, 
 * @param type, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @return an allocated connection on the open tcp socket
 */
extern GW_CONN_PTR gw_create_new_client_udp_connection(
    const char * remote_host, int remote_port)
{
  int fd;
  GW_CONN_PTR conn_;
  struct sockaddr *addr = NULL;
  socklen_t len;

  INFO("gw_create_new_client_udp_connection");

  char serv[NI_MAXSERV];
  snprintf(serv, sizeof(serv), "%d", remote_port);

  if ((fd = sock_init_udp_connection(remote_host, remote_port, &addr, &len))
      < 0)
  {
    WARN("Failed to open a connection to the host: %s, port:%i",remote_host,remote_port);
    return NULL;
  }

  //Set the conn_->remote_addr for sending later data on this socket !

  conn_ = gw_conn_alloc(fd);
  conn_->remote_addr = (struct sockaddr_storage*) addr;
  conn_->remote_addr_len = len;

  gw_conn_set_status(conn_, CONN_OPEN);//22/04/09

  //conn_->status=CONN_OPEN;

  INFO("Sucessfully connected to %s",sock_ntop(addr));

  /*
   addr=sock_get_addrinfo(remote_host,serv, SOCK_DGRAM);
   INFO("try to init connect to %s",sock_ntop((SA*)addr->ai_addr));
   conn_->remote_addr=malloc(addr->ai_addrlen);
   memcpy(conn_->remote_addr,addr->ai_addr,addr->ai_addrlen);
   INFO("test copy addr: %s",sock_ntop((SA*)conn_->remote_addr));
   conn_->remote_addr_len=addr->ai_addrlen;
   freeaddrinfo(addr);
   */
  //conn_->len=len;
  INFO("Connection created to connect to: %s with socket:%d",sock_ntop((SA *)conn_->remote_addr),fd);

  INFO("gw_create_new_client_udp_connection ended");
  return conn_;
}

extern int gw_conn_nb_rbytes(GW_CONN_PTR conn)
{
  return conn->rbuflenused;
}

/**
 * Receive bytes from a given connection using an udp connection
 * 
 * @param conn, an allocated connection with a udp opened socket 
 * @return int, the number of bytes read, <code>EXIT_FAILURE</code>
 */
extern void* gw_conn_udp_recv(void * arg)
{
  INFO("gw_conn_udp_recv");

  GW_CONN_PTR conn = (GW_CONN_PTR) arg;

  _lock(&conn->rlock);

  int ret = 0;
  int done;
  size_t start, total, left;
  uint8_t *pos;

  struct sockaddr_storage ss;

  total = start = 0;
  left = conn->rbuflen;
  pos = (uint8_t*) conn->rbuf;
  done = 0;
  socklen_t len = sizeof(ss);

  ret = recvfrom(conn->sock, pos, left, 0, (SA*) &ss, &len);
  INFO("Test remote_addr after receiving: %s",sock_ntop((SA*)&ss));

  _lock(&conn->alock);
  if (conn->remote_addr != NULL)
  {
    WARN("Strange remote addr is already set up ?");
    WARN("No this socket is initialized by either (1) the server thread or (2) gw_init_udp_connection");
    WARN("if (1) remote_addr is NULL ; if (2) remote_addr is set up ");
    INFO("Test remote_addr before receiving: %s",sock_ntop((SA*)conn->remote_addr));
    INFO("Test remote_addr after  receiving: %s",sock_ntop((SA*)&ss));
    INFO("Free previous remote_addr: %s",sock_ntop((SA*)conn->remote_addr));
    free(conn->remote_addr);
    conn->remote_addr = NULL;
  }

  //Set conn->remote_addr to send back an ack or a message to the sender of the current message
  //So a malloc as structure ss is locally allocated
  conn->remote_addr = malloc(len);
  memcpy(conn->remote_addr, &ss, len);
  conn->remote_addr_len = len;
  _unlock(&conn->alock);

  INFO(">>>>> %s |len: %d",sock_ntop((SA*)conn->remote_addr),conn->remote_addr_len);

  if (ret == -1)
  {
    gw_conn_set_status(conn, CONN_READ_ERROR),
    //conn->status=CONN_READ_ERROR;
    conn->rbuflenused = -1;
    switch (errno)
    {

    case EAGAIN:
      ERR("ERROR: recvfrom:[%d] %s\n", errno, strerror(errno));
      (void) pthread_mutex_unlock(&conn->lock);
      exit(EXIT_FAILURE);
      break;

    case EINTR:
      ERR("ERROR: recvfrom:[%d] %s\n", errno, strerror(errno));
      (void) pthread_mutex_unlock(&conn->lock);
      exit(EXIT_FAILURE);
      break;

    default:
      ERR("Failed to receive data from udp socket");
      (void) pthread_mutex_unlock(&conn->lock);
      exit(EXIT_FAILURE);
    }
  }
  else if (ret == 0)
  {
    ERR("Error no data received yet");
    return NULL;
  } // end error management


  //  INFO("Receiving udp request from %s on fd %d", sock_ntop((SA*)&ss), conn->sock);    


  total += (size_t) ret;
  left -= (size_t) ret;
  pos = ((uint8_t *) conn->rbuf) + total;

  //  INFO("%d bytes reveived (total=%u) from %s on fd %d", (int)ret, (unsigned int) total,sock_ntop((SA*)&ss), conn->sock);  

  *pos = '\0';

  //  unlock_conn(conn);

  _lock(&conn->ilock);
  conn->bytes_out += total;
  conn->rbuflenused = total;
  conn->msg_in++;
  _unlock(&conn->ilock);

  _unlock(&conn->rlock);

  //INFO("udp recv: %s",conn->rbuf);
  return NULL;
}

/**
 * Receive bytes from a given connection using a tcp connection
 * WARNING, the call is blocked until the tcp socket is closed
 *
 * @param conn, an allocated connection with a tcp opened socket 
 * @return int, the number of bytes read, <code>EXIT_FAILURE</code> otherwise
 */
extern void* gw_conn_tcp_recv(void * arg)
{
  INFO("gw_conn_tc_recv");
  GW_CONN_PTR conn = (GW_CONN_PTR) arg;
  char addr_str[32];
  int ret, done, type;
  size_t start, total, left;
  socklen_t size;
  uint8_t *pos;

  total = start = 0;
  left = conn->rbuflen;
  pos = (uint8_t*) conn->rbuf;
  done = 0;
  size = (socklen_t) sizeof(type);

  //  lock_conn(conn);

  INFO("Receiving tcp request from %s on fd %d", addr_str, conn->sock);

  INFO("Receiving tcp request on fd %d",conn->sock);

  do
  {
    if ((ret = recv(conn->sock, pos, left, 0)) == 0)
    {
      INFO("connection closed");
      conn->status = CONN_CLOSE;
      if (total == 0)
      {
        //(void)pthread_mutex_unlock(&conn->lock);
        conn->rbuflenused = 0;
        return (NULL);
      }
      break;
    }
    else if (ret == -1)
    {
      if (errno == EAGAIN)
        continue;
      ERR("Receive error : %s",strerror(errno));
      conn->rbuflenused = 0;
      //(void)pthread_mutex_unlock(&conn->lock);
      return (NULL);
    }

    total += (size_t) ret;
    left -= (size_t) ret;
    INFO("%d bytes reveived (total=%u) on fd %d", (int)ret, (unsigned int) total, conn->sock);
    pos = ((uint8_t *) conn->rbuf) + total;

    if (left == 0)
    {
      gw_conn_grow_rbuf(conn, conn->rbuflen + 1024);
      left = 1024;
    }
    else
    {
      done = 1;
    }
  } while (!done);

  *pos = '\0';

  conn->rbuflenused = total;
  conn->bytes_out += total;

  //  unlock_conn(conn);

  INFO("tcp recv : %s", conn->rbuf);
  return NULL;
}

/**
 * Receive bytes from a given connection using either an udp or tcp connection
 *
 * @param conn, an allocated connection with an opened socket 
 * @return int, the number of bytes read, FALSE otherwise
 */
extern void* gw_conn_recv(void * arg)
{

  INFO("gw_conn_recv");
  GW_CONN_PTR conn = (GW_CONN_PTR) arg;

  char addr_str[32];
  int ret, done, type;
  size_t start, total, left;
  socklen_t size;
  uint8_t *pos;

  total = start = 0;
  left = conn->rbuflen;
  pos = (uint8_t*) conn->rbuf;
  done = 0;
  size = (socklen_t) sizeof(type);

  //  lock_conn(conn);

  if (getsockopt(conn->sock, SOL_SOCKET, SO_TYPE, (char*) &type, &size) < 0)
  {
    ERR("Failed to get the type of the socket connection: %s ", strerror(errno));
    (void) pthread_mutex_unlock(&conn->lock);
    conn->status = CONN_UNKNOWN;
    return (NULL);
  }

  if (type == SOCK_STREAM)
  {
    inet_ntop((*(SA*) conn->remote_addr).sa_family,
        &((struct sockaddr_in *) &conn->remote_addr)->sin_addr, addr_str,
        sizeof(addr_str));

    INFO("Receiving tcp request from %s on fd %d", addr_str, conn->sock);
    do
    {
      if ((ret = recv(conn->sock, pos, left, 0)) == 0)
      {
        INFO("connection closed");
        conn->status = CONN_CLOSE;
        if (total == 0)
        {
          (void) pthread_mutex_unlock(&conn->lock);
          conn->rbuflenused = -1;
          return (NULL);
        }
        break;
      }
      else if (ret == -1)
      {
        if (errno == EAGAIN)
          continue;
        conn->status = CONN_UNKNOWN;
        conn->rbuflenused = -1;
        ERR("Receive error : %s",strerror(errno));
        (void) pthread_mutex_unlock(&conn->lock);
        return (NULL);
      }
      total += (size_t) ret;
      left -= (size_t) ret;
      INFO("%d bytes reveived (total=%u) from %s on fd %d", (int)ret, (unsigned int) total,addr_str, conn->sock);
      pos = ((uint8_t *) conn->rbuf) + total;

      if (left == 0)
      {
        gw_conn_grow_rbuf(conn, conn->rbuflen + 1024);
        left = 1024;
      }
      else
      {
        done = 1;
      }
    } while (!done);
  }
  else if (type == SOCK_DGRAM)
  {
    ret = recvfrom(conn->sock, pos, left, 0, (SA*) conn->remote_addr,
        &conn->remote_addr_len);
    inet_ntop((*(SA*) conn->remote_addr).sa_family,
        &((struct sockaddr_in *) &conn->remote_addr)->sin_addr, addr_str,
        sizeof(addr_str));
    INFO("Receiving udp request from %s on fd %d", addr_str, conn->sock);

    if (ret == -1)
    {
      switch (errno)
      {

      case EAGAIN:
        WARN("Packet with bad checksum received\n");
        break;

      case EINTR:
        ERR("ERROR: recvfrom:[%d] %s\n", errno, strerror(errno));
        break;

      default:
        ERR("Failed to receive data from udp socket");
        conn->rbuflenused = -1;
        conn->status = CONN_UNKNOWN;
        (void) pthread_mutex_unlock(&conn->lock);
        return (NULL);
      }
    }// end error management

    total += (size_t) ret;
    left -= (size_t) ret;
    pos = ((uint8_t *) conn->rbuf) + total;
    INFO("%d bytes reveived (total=%u) from %s on fd %d", (int)ret, (unsigned int) total,addr_str, conn->sock);
  }
  else
  {
    ERR("Type of the socket connection is neither SOCK_STREAM nor SOCK_DGRAM");
    conn->status = CONN_UNKNOWN;
    conn->rbuflenused = -1;
    (void) pthread_mutex_unlock(&conn->lock);
    return (NULL);
  }

  *pos = '\0';

  conn->rbuflenused = total;
  conn->bytes_out += total;

  //  unlock_conn(conn);
  return NULL;
}

/**
 * Close the socket used the given connection
 *
 * @param conn, an allocated connection with an opened socket 
 * @return  <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int gw_conn_close(GW_CONN_PTR conn)
{
  int ret;
  //  lock_conn(conn);
  conn->status = CONN_CLOSE;
  ret = sock_close(conn->sock);
  //  unlock_conn(conn);
  return ret;
}

/**
 * Allocate a new gateway connection structure and return it.
 *
 * @param sock, A socket previously open
 * @return GW_CONN_PTR, A pointer to a connection structure 
 *          or <code>NULL</code> if an error occured.
 */
extern GW_CONN_PTR gw_conn_alloc(int sock)
{
  //  INFO("gw_conn_alloc");

  if (sock_is_a_socket(sock) == 0)
    WARN("The fd %d is not a not a valid socket",sock);

  GW_CONN_PTR conn = NULL;

  if ((conn = (GW_CONN_PTR) malloc(sizeof(gw_conn_t))) == NULL)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    return (NULL);
  }

  conn->len = (socklen_t) 0;
  conn->local_addr = NULL;
  conn->remote_addr = NULL;
  conn->status = CONN_UNKNOWN; //set status to unknown
  conn->sock = sock;
  conn->remote_addr_len = sizeof(struct sockaddr);
  conn->local_addr_len = sizeof(struct sockaddr);
  conn->rbuflen = CONN_INITBUFSIZE;
  conn->wbuflen = CONN_INITBUFSIZE;
  conn->wbuflenused = 0;
  conn->rbuflenused = 0;
  conn->bytes_out = 0;
  conn->bytes_in = 0;
  conn->msg_out = 0;
  conn->msg_in = 0;
  conn->flags = 0;

  conn->remote_addr = NULL;
  conn->local_addr = NULL;
  conn->z2z_lock = NULL;
  conn->recv_hdlr = NULL;

  if ((conn->rbuf = malloc(sizeof(char) * conn->rbuflen)) == NULL)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  conn->rbuf_ptr = conn->rbuf;

  if ((conn->wbuf = malloc(sizeof(char) * conn->wbuflen)) == NULL)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  conn->wbuf_ptr = conn->wbuf;

  if (pthread_mutex_init(&conn->lock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  if (pthread_mutex_init(&conn->rlock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  if (pthread_mutex_init(&conn->wlock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  if (pthread_mutex_init(&conn->ilock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  if (pthread_mutex_init(&conn->slock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  if (pthread_mutex_init(&conn->alock, NULL) != 0)
  {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    gw_conn_free(conn);
    return (NULL);
  }

  //  INFO("gw_conn_alloc ended");
  return (conn);
}
