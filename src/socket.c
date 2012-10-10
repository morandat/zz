#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "z2z_log.h"    // for macro ERR, WARN, INFO
#include <netinet/in.h> // for struct sockaddr_in, group_req 
#include <string.h>     // for method strerror()
#include <unistd.h>     // for method close()
#include <arpa/inet.h>  // for method inet_ntoa()
#include <errno.h>      // for variable errno
#include "z2z_socket.h"
#include <netdb.h>      //for gethostbyname
#include <sys/socket.h>

#include <assert.h> 
#include <net/if.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdlib.h>
#ifdef	HAVE_SOCKADDR_DL_STRUCT
# include	<net/if_dl.h>
#endif

#ifdef	__APPLE__
#include <fcntl.h>
#include <arpa/inet.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#endif



/** 
 * Number of connections allowed on the incomming queue
 */
#define BACKLOG 10 
#define BUFFER_INCREMENT	2048      //Control udp socket internal buffer
#define MAX_RECV_BUFFER_SIZE	256*1024  //Control udp socket internal buffer 

//static int sock_init_connection(const struct sockaddr_in *addr, const int type, int protocol);
//static int soCk_init_sockaddr(struct sockaddr_in *addr, const char *hostname, unsigned short int port);
static int sock_probe_max_receive_buffer(int udp_sock);

/**
 * Increase the buffer as much as possible for udp socket
 * @param udp_sock An udp socket
 * @return TRUE if success, otherwise FALSE
 */
static int sock_probe_max_receive_buffer( int udp_sock )
{
  int optval;
  int ioptval;
  unsigned int ioptvallen;
  int foptval;
  unsigned int foptvallen;
  int voptval;
  unsigned int voptvallen;
  int phase=0;
  
  ioptvallen=sizeof(ioptval);
  if (getsockopt( udp_sock, SOL_SOCKET, SO_RCVBUF, (void*) &ioptval,
		  &ioptvallen) == -1 )
    {
      ERR("ERROR: udp_init: getsockopt: %s", strerror(errno));
      return -1;
    }
  
  if ( ioptval==0 ) 
    {
      INFO("DEBUG: udp_init: SO_RCVBUF initially set to 0; resetting to %d",
	   BUFFER_INCREMENT );
      ioptval=BUFFER_INCREMENT;
    } 
  else INFO("INFO: udp_init: SO_RCVBUF is initially %d", ioptval );
  
  for (optval=ioptval; ;  ) {
    // increase size; double in initial phase, add linearly later
    if (phase==0) optval <<= 1; else optval+=BUFFER_INCREMENT;
    if (optval > MAX_RECV_BUFFER_SIZE ){
      if (phase==1) break; 
      else { phase=1; optval >>=1; continue; }
    }
    
    //    INFO("DEBUG: udp_init: trying SO_RCVBUF: %d", optval );
    if (setsockopt( udp_sock, SOL_SOCKET, SO_RCVBUF,
		    (void*)&optval, sizeof(optval)) ==-1){
      // Solaris returns -1 if asked size too big; Linux ignores
      INFO("DEBUG: udp_init: SOL_SOCKET failed"
	   " for %d, phase %d: %s", optval, phase, strerror(errno));
      //if setting buffer size failed and still in the aggressive
      // phase, try less aggressively; otherwise give up 
      
      if (phase==0) { phase=1; optval >>=1 ; continue; } 
      else break;
    } 
    // verify if change has taken effect 
    // Linux note -- otherwise I would never know that; funny thing: Linux
    //   doubles size for which we asked in setsockopt

    voptvallen=sizeof(voptval);
    if (getsockopt( udp_sock, SOL_SOCKET, SO_RCVBUF, (void*) &voptval,
		    &voptvallen) == -1 )
      {
	ERR("ERROR: udp_init: getsockopt: %s", strerror(errno));
	return -1;
      } else {
	//      INFO("DEBUG: setting SO_RCVBUF; set=%d,verify=%d",optval, voptval);
      if (voptval<optval) {
	INFO("DEBUG: setting SO_RCVBUF has no effect");
	//if setting buffer size failed and still in the aggressive
	//  phase, try less aggressively; otherwise give up 
	
	if (phase==0) { phase=1; optval >>=1 ; continue; } 
	else break;
      } 
    }
    
  } /* for ... */
  foptvallen=sizeof(foptval);
  if (getsockopt( udp_sock, SOL_SOCKET, SO_RCVBUF, (void*) &foptval,
		  &foptvallen) == -1 )
    {
      ERR("ERROR: udp_init: getsockopt: %s", strerror(errno));
      return -1;
    }
  INFO("INFO: udp_init: SO_RCVBUF is finally %d", foptval );
  return 0;  
}

/**
 * Check if data is available, if not, wait timeout seconds for data
 * to be present.
 * @param socket A socket
 * @param timeout How long to wait before timeout (value in seconds)
 * @return TRUE if the event occured, otherwise FALSE.
 */
extern int sock_can_read(int socket, int timeout) {
  ERR("sock_can_read starting");
  int r= 0;
  fd_set rset;
  struct timeval tv;
  
  FD_ZERO(&rset);
  FD_SET(socket, &rset);
  tv.tv_sec= timeout;
  tv.tv_usec= 0;
  
  do {
    r= select(socket+1, &rset, NULL, NULL, &tv);
  } while(r == -1 && errno == EINTR);
  return (r > 0);
}

/**
 * Return true if socket has successfully connected
 */
int sock_has_connected(int tcp_sock)
{
  int optval;
  socklen_t optlen = sizeof(optval);
  int result = getsockopt(tcp_sock, SOL_SOCKET, SO_ERROR,
			  &optval, &optlen);
  
  if( (result) || ! (optlen == sizeof(optval)))
    WARN("Error");
  
  return !optval;
}

/**
 * Read up to size bytes from the <code>socket</code> into the
 * <code>buffer</code>. If data is not available wait for
 * <code>timeout</code> seconds.
 * @param socket the Socket to read data from
 * @param buffer The buffer to write the data to
 * @param size Number of bytes to read from the socket
 * @param timeout Seconds to wait for data to be available
 * @return The number of bytes read or -1 if an error occured. 
 */
int sock_read(int socket, void *buffer, int size, int timeout) {
  //  ERR("sock_read starting");
  ssize_t n;

  if(size<=0)
      return 0;
  //  errno= 0;
  do {
    n= read(socket, buffer, size);
  } while(n == -1 && errno == EINTR);
  
  if(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    if(! sock_can_read(socket, timeout)) {
      return -1;
    }
    do {
      n= read(socket, buffer, size);
    } while(n == -1 && errno == EINTR);
  }
  //WARN("sock_read ending");
  return n;
}


/**
 * Init a local TCP connection to connect to a remote server located 
 * at <code>remote_host</code> address using <code> remote_port</code>
 * 
 * Note: Select the first available network interface as the source address
 * Warning if the host has several network interfaces 
 *
 * @param remote_port, the remote socket port to connect to
 * @param remote_host, the string corresponding to the remote address (dns name accepted)
 * @return the socket id or -1 if an error occured.
 */
/*
extern int sock_init_tcp_connection(const char * remote_host, int remote_port)
{
  //Convert int port to string port
  char serv[NI_MAXSERV];
  snprintf (serv, sizeof(serv), "%d", ntohs(remote_port)); 
  return sock_init_connection(remote_host, serv,SOCK_STREAM);
}
*/

extern int sock_init_tcp_connection(const char * remote_host, int remote_port, socklen_t *len)
{
  //Convert int port to string port
  char serv[NI_MAXSERV];
  snprintf (serv, sizeof(serv), "%d", remote_port); 

  struct addrinfo *ai,*sav;
  int sockfd;

  //  INFO("host: %s , port: %s", remote_host,serv);
  
  ai=sock_get_addrinfo(remote_host,serv,SOCK_STREAM,AF_UNSPEC);  
  sav=ai;

  do {
    //INFO("Try to connect to host:%s\n",sock_ntop((SA*)ai->ai_addr));
    sockfd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sockfd<0)
      {
	//INFO("unable to create socket :%s",strerror(errno));
	continue;
      }

    if(connect(sockfd, (SA*)ai->ai_addr, ai->ai_addrlen) ==0)
      break; // success 

    //INFO("unable to connect to socket %d :%s",sockfd, strerror(errno));    
    //INFO("Unable to connect to host:%s len:%d\n",sock_ntop((SA*)ai->ai_addr), ai->ai_addrlen);

    close(sockfd);
    
  }while ((ai=ai->ai_next)!=NULL);

  if (ai ==NULL)
    {
      ERR("Failed to connect to host:%s:%s\n",remote_host,serv);
      exit(-1);
    }
  
  INFO("Connection created to connect to host:%s from fd #%d\n",sock_ntop((SA*)ai->ai_addr),sockfd);
  
  if(len)
    *len=ai->ai_addrlen;
  
  freeaddrinfo(sav);
  return (sockfd);
}



/**
 * Init a local UDP connection to connect to a remote server located 
 * at <code>remote_host</code> address using <code> remote_port</code>
 * 
 * Note: Select the first available network interface as the source address
 * Warning if the host has several network interfaces 
 *
 * @param remote_port, the remote socket port to connect to
 * @param remote_host, the string corresponding to the remote address (dns name accepted)
 * @return the socket id or -1 if an error occured.
 */
/*
extern int sock_init_udp_connection(const char * remote_host, int remote_port)
{
  INFO("sock_init_udp_connection");
  //Convert int port to string port
  char serv[NI_MAXSERV];
  snprintf (serv, sizeof(serv), "%d", ntohs(remote_port)); 
  return sock_init_connection(remote_host, serv,SOCK_DGRAM);
}
*/

extern int sock_init_udp_connection(const char * remote_host, int remote_port, SA** sa_ptr,socklen_t *len)
{
  INFO("sock_init_udp_connection");
  //Convert int port to string port
  char serv[NI_MAXSERV];
  snprintf (serv, sizeof(serv), "%d", remote_port); 
  struct addrinfo *ai,*sav;
  int sockfd;

  ai=sock_get_addrinfo(remote_host,serv,SOCK_DGRAM,AF_UNSPEC);  
  sav=ai;

  do {
    sockfd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sockfd>=0)
      break; // success 
  }while ((ai=ai->ai_next)!=NULL);
  
  if (ai ==NULL)
    INFO("Failed to create a new socket, host:%s , port:%s\n",sock_ntop(ai->ai_addr) ,sock_get_port((SA*)ai));

  *sa_ptr = malloc(ai->ai_addrlen);
  memcpy(*sa_ptr, ai->ai_addr, ai->ai_addrlen);
  *len=ai->ai_addrlen;

  WARN("OK create a new socket, host:%s , port:%d, sockid: %d\n",sock_ntop((SA *)ai->ai_addr), sock_get_port((SA*)ai),sockfd);  
  freeaddrinfo(sav);

  return (sockfd);
}

/**
 * Init a local connection either in a tcp or udp mode to connect to a remote server located 
 * through its addresse structure <code>const sockaddr_in *addr</code>
 * 
 * Note: Select the first available network interface as the source address
 * Warning if the host has several network interfaces 
 *
 * @param addr, a pointer to a sockaddr_in strucure already initialized 
 * @param type, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @param protocol, specifies a particular protocol to be used with the socket 
 * e.g. IPPROTO_TCP for tcp,  IPPROTO_UDP for udp
 * @return the socket id or -1 if an error occured.
 */


/**
 * Use first a local @addr to bind to 
 * And then use a remote @ to send to
 */
/*
extern int sock_init_connection(const char *remote_host, const char *serv, int type){
  INFO("sock_init_connection");
  int sockfd;

  struct addrinfo *lai,*rai;
  
  lai=sock_get_addrinfo(NULL,"0",type); //Get a random port and one @IP from a random interface
  
  rai=sock_get_addrinfo(remote_host,serv,type);
  sockfd=sock_create_valid_socket(lai);// create and bind to this socket
    
  freeaddrinfo(lai);
  
  if (rai ==NULL)
    {  
      INFO("Failed to allocate an addr corresponding to the host:%s , port:%s\n", remote_host,serv);
      exit(-1);
    }
  
  if((connect(sockfd, rai->ai_addr, rai->ai_addrlen))<0)
    {
      ERR("Failed to connect to host: %s, %s with socket:%d",sock_ntop((SA*) rai->ai_addr),strerror(errno),sockfd);
      exit(-1);
    }
  
  freeaddrinfo(rai);
  return (sockfd);
} 
*/

/*
extern int sock_init_connection(const char *remote_host, const char *serv, int type){
  INFO("sock_init_connection");
  int sockfd;
  struct addrinfo *ai;

  ai=sock_get_addrinfo(remote_host,serv,type);
  sockfd=sock_init_connection_from_addrinfo(ai);
  freeaddrinfo(ai);
  return (sockfd);
} 
*/

/**
 *
 */
/*
extern int sock_init_connection_from_addrinfo(const struct addrinfo* ai){
  INFO("sock_init_connection_from_addrinfo");
  int sockfd;
  
  do {
    sockfd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sockfd<0)
      continue;
    
    if(connect(sockfd, ai->ai_addr, ai->ai_addrlen) ==0)
      break; // success 
    
    close(sockfd);
    
  }while ((ai=ai->ai_next)!=NULL);
  
  if (ai ==NULL)
    INFO("Failed to create a new socket, host:%s , port:%s\n",sock_ntop((SA*)ai) ,sock_get_port((SA*)ai));
  
  return (sockfd);
 
} 
*/


/**
 *
 * if remote_host == NULL then flags is AI_PASSIVE
 * In other word, we try to get an addr for a socket server
 * 
 * if remàte_host != NULL then we try to get an active socket
 */
extern struct addrinfo *sock_get_addrinfo(const char *remote_host, const char *remote_port, int type, short family)
{
  //  INFO("sock_get_addrinfo");
  struct addrinfo *ai, hints;
  int n;

  if(remote_host==NULL)
    hints.ai_flags = AI_PASSIVE;
  
  hints.ai_flags |= AI_NUMERICHOST;
  
  bzero(&hints, sizeof(struct addrinfo));
  hints.ai_socktype = type;
  if(family<=0)
    hints.ai_family = AF_UNSPEC;
  else
    hints.ai_family = family;

  if((n=getaddrinfo(remote_host, remote_port, &hints, &ai))!=0)
  {  
    ERR("Failed to create a new socket, host:%s , port:%s, %s\n", remote_host,remote_port,gai_strerror(n));
    exit(-1);
  }

  return ai;  
}


/**
 * Create a valid socket bound to the specified IP address and port
 * at <code>hostname</code> address using <code> port</code>
 *
 * @param server_port, the remote socket port to connect to
 * @param hostname, the string corresponding to the ip address
 * @param type, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @param protocol, specifies a particular protocol to be used with the socket 
 * e.g. IPPROTO_TCP for tcp,  IPPROTO_UDP for udp
 * @return the socket id or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int sock_create_valid_socket(const struct addrinfo *ai)
{
  // INFO("sock_create_valid_socket");  
  const int on =1;
  int sockfd;

  do {
    sockfd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sockfd<0)
      continue;
    
    if(setsockopt(sockfd, 
		   SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on)) == -1 ) {
      WARN("Setting socket options: %s.\n", strerror(errno));
    }

    if(bind(sockfd, ai->ai_addr, ai->ai_addrlen) ==0)
       break; /* success */
    
    close(sockfd);
    
  }while ((ai=ai->ai_next)!=NULL);

  INFO("Interface selected: %s",sock_ntop((SA*)ai->ai_addr));
  
  if (ai ==NULL)
    {
      ERR("Bind() error lol! : %s",strerror(errno) );
      INFO("Bind() error ...port: %d  addr: %s", 
	   sock_get_port(ai->ai_addr),sock_ntop((SA *) ai));
    }
  return (sockfd);
}

  
/**
 * Send one lines of text <code>string<code> on the <code>socket</code> 
 * @param socket the socket to write to
 * @param string The string to write
 * @return The number of bytes sent or -1 if an error occured.
 */
extern int sock_send_string(int socket, const char *string)
{
  return sock_tcp_send (socket, string, strlen(string),0);
} 

/**
 * Close the <code>socket<code>
 * @param socket the socket to close
 * @return Return TRUE if the event occured, otherwise FALSE
 */
int sock_close (int socket)
{/*
  int err;
  err = shutdown (socket, SHUT_RDWR);
  if (!err)
    close (socket);
    return err;*/
  // shutdown(socket,SHUT_RDWR);
  close(socket);
   return EXIT_SUCCESS;
} 

/**
 * Send <code>size</code> bytes from the <code>src</code> to the
 * <code>socket</code> 
 * @param socket the socket to write to
 * @param buffer The buffer to write
 * @param size Number of bytes to send
 * @param timeout Seconds to wait for data to be written
 * @return The number of bytes sent or -1 if an error occured.
 */
extern int sock_tcp_send (int socket, const void *src, size_t size,int timeout)
{
  int offset = 0;
  int ret;
    
  if (!src || size <= 0)
    {
      ERR("Unable to send data on socket %d, nothing to send ;-)",socket);
      return EXIT_FAILURE;
    }
  
  // write isn't guaranteed to send the entire string at once,
  // so we have to sent it in a loop 
  while (offset != size) {  
    ret = send(socket, ((char *) src) + offset, size - offset, 0);
    if(ret == -1){
      switch(ret){
      case EAGAIN:
	//case EWOULDBLOCK: 
	if(! sock_can_write(socket, timeout)) {
	  ERR("FAILED to send data on socket %d",socket);	
	  return EXIT_FAILURE;
	}
	break;
      default:
	ERR("FAILED to send data on socket %d, %s",socket,strerror(errno));	
	return ret;
      }
    } else if (ret ==0) {
      WARN("socket has been closed by the remote peer");
      return ret+offset;
    }
    offset += ret;
  }
  return offset;
}

/**
 * Send <code>size</code> bytes from the <code>src</code> to the
 * <code>socket</code> 
 * @param socket the socket to write to
 * @param buffer The buffer to write
 * @param size Number of bytes to send
 * @param timeout Seconds to wait for data to be written
 * @return The number of bytes sent or -1 if an error occured.
 */
extern int sock_udp_send (int socket, const void *src, size_t size, const struct sockaddr *dest_addr,socklen_t dest_len,int timeout)
{
  int ret;
  
  //  INFO(">>>sock_udp_send");
  
  //  INFO("Send to:%s",sock_ntop(dest_addr));
  
  if (!src || size <= 0)
    {
      ERR("Unable to send data on socket %d, nothing to send ;-)",socket);
      return EXIT_FAILURE;
    }
  
  do {
    //INFO("msg: %s",src);
    assert(dest_addr!=NULL);
    INFO("send");
    ret=sendto(socket, src, size, 0, dest_addr, dest_len);
    //ret=send(socket, src, size, 0); // 26/O4/O8 apparently do not work when socket is multicast
  } while(ret == -1 && errno == EINTR);
  
  if(ret == -1 && (errno != EAGAIN || errno != EWOULDBLOCK))  
    {
      ERR("Unable to send data on socket %d, %s",socket,strerror(errno));
      return EXIT_FAILURE;
    }
  return ret;
}

/**
 * It waits on a connected socket and it manages to receive exactly 'size' bytes.
 * This function basically calls the recv() socket function and it checks that no
 * error occurred.
 *
 *
 */
int sock_receive(int socket, char* buffer, int size, char *errbuf)
{
  /*
  nt nread;
  int totread= 0;
  
  if (size == 0)
    {
      SOCK_ASSERT("I have been requested to read zero bytes", 1);
      return 0;
    }
 again:
  nread= recv(sock, &(buffer[totread]), size - totread, 0);
  if (nread == -1)
    {
      sock_geterror("recv(): ", errbuf, SOCK_ERRBUF_SIZE);
      return -1;
    }
  if (nread == 0)
    {
      snprintf(errbuf, SOCK_ERRBUF_SIZE, "The other host terminated the connection.");
      return -1;
    }
  totread+= nread;
  if (totread != size)
    goto again;
  return totread;
  } */
return 0;
}

/**
* Check if data can be sent to the <code>socket</code>, if not, wait timeout
* seconds for the <code>socket</code> to be ready.
* @param socket A socket
* @param timeout How long to wait before timeout (value in seconds)
* @return Return TRUE if the event occured, otherwise FALSE.
*/
extern int sock_can_write(int socket, int timeout) {

  int r= 0;
  fd_set wset;
  struct timeval tv;

  FD_ZERO(&wset);
  FD_SET(socket, &wset);
  tv.tv_sec= timeout;
  tv.tv_usec= 0;
  
  do {
    r= select(socket+1, NULL, &wset, NULL, &tv);
  } while(r == -1 && errno == EINTR);
  return (r > 0);
} 

/**
 * Determine if a file descriptor <code>fd</code> is in fact a socket
 *
 * @param fs, A socket
 * @return TRUE if it is a socket, otherwise FALSE
 */
extern int sock_is_a_socket(int socket)
{
	int v;
	socklen_t l = sizeof (int);

	/* Parameters to getsockopt, setsockopt etc are very
	 * unstandardized across platforms, so don't be surprised if
	 * there are compiler warnings on e.g. SCO OpenSwerver or AIX.
	 * It seems they all eventually get the right idea.
	 *
	 * Debian says: ``The fifth argument of getsockopt and
	 * setsockopt is in reality an int [*] (and this is what BSD
	 * 4.* and libc4 and libc5 have).  Some POSIX confusion
	 * resulted in the present socklen_t.  The draft standard has
	 * not been adopted yet, but glibc2 already follows it and
	 * also has socklen_t [*]. See also accept(2).''
	 **/

	return getsockopt(socket, SOL_SOCKET, SO_TYPE, (char *)&v, &l) == 0;
}

/**
 * Enable nonblocking i|o on the given <code>socket</code>.
 * @param socket A socket
 * @return TRUE if success, otherwise FALSE
 */
extern int sock_set_noblock(int socket) {
  
  int flags;

  flags= fcntl(socket, F_GETFL, 0);
  flags |= O_NONBLOCK;

  return (fcntl(socket, F_SETFL, flags) == 0);

}

/**
 * Disable nonblocking i|o on the given <code>socket</code>
 * @param socket, A socket
 * @return TRUE if success, otherwise FALSE
 */
extern int sock_set_block(int socket) {

  int flags;

  flags= fcntl(socket, F_GETFL, 0);
  flags &= ~O_NONBLOCK;

  return (fcntl(socket, F_SETFL, flags) == 0);

}

/**
 * Create server socket that listens on one <code>port</code>
 *
 * @param port, port number of the server socket
 * @param type, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @param protocol, specifies a particular protocol to be used with the socket 
 * e.g. IPPROTO_TCP for tcp,  IPPROTO_UDP for udp
 * @return int, the socket file descriptor
 */
extern int sock_create_srvsock_on_specific_interface(const char* remote_host, unsigned short remote_port, int type)
{
  //INFO("sock_create_srvsock_on_specific_interface");  
  int sockfd,n;
  const int on = 1;
  struct addrinfo *ai, hints, *sav; 
  char serv[NI_MAXSERV];

  bzero(&hints, sizeof(struct addrinfo));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_socktype = type;
  hints.ai_family = AF_UNSPEC;
  
  snprintf (serv, sizeof(serv), "%d", remote_port);   
  
  if((n=getaddrinfo(remote_host, serv, &hints, &ai))!=0)
    ERR("Failed to create a new socket, host:%s , port:%s, %s\n", remote_host,gai_strerror(n));
  
  sav=ai;

  do {
    sockfd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sockfd<0)
      continue;
    
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(bind(sockfd, ai->ai_addr, ai->ai_addrlen) ==0)
       break; /* success */
    
    close(sockfd);
    
  }while ((ai=ai->ai_next)!=NULL);
  
  if (ai ==NULL)
    {
      ERR("Failed to create a new socket, host:%s , port:%s\n", remote_host,serv);
      return (-1);
    }
  INFO("Create new socket, %s\n",sock_ntop(ai->ai_addr));  
  
  if(type == SOCK_STREAM){
      if( listen(sockfd, BACKLOG) == -1 ){
	ERR("listen() error lol %s!", strerror(errno));
	close(sockfd);
	return -1;
      }
  }else if (type == SOCK_DGRAM){
      if(sock_probe_max_receive_buffer(sockfd)== -1){
	  ERR("Unable to increase buffer for udp socket");
	}
    }
  /*
  if (len)
    *len = ai->ai_addrlen; // return size of protocol address 
  */
  freeaddrinfo(sav);
  return (sockfd);  
}

/**
*
*
*/
extern int sock_create_srvsock(unsigned short port, int type)
{
  return sock_create_srvsock_on_specific_interface(NULL, port, type);
}

    
/**
 * Helper function to create a server socket listening on a specified port
 * from a service name. Automatically get port number of the socket from sytem file /etc/services
 * @param service, name of the service
 * @param type,   socket type like "udp" or "tcp"
 * @return int, the socket file descriptor
 */
extern int sock_create_srvsock_from_service(const char *service, const char *type)
{
  ERR("Not yet implemented, one day perhaps lol");
  return (-1);
}

/**
 * Helper function to create a udp server socket listening on a specified port
 *
 * @param port, port number of the server socket
 * @return int, the socket file descriptor
 */
extern int sock_create_udp_srvsock(unsigned short port)
{
  return sock_create_srvsock(port, SOCK_DGRAM);
}

/**
 * Helper function to create a tcp server socket listening on a specified port
 *
 * @param port, port number of the server socket
 * @return int, the socket file descriptor
 */
extern int sock_create_tcp_srvsock(unsigned short port)
{
  return sock_create_srvsock(port, SOCK_STREAM);
}

/**
*
*
*/
extern int sock_set_multicast_from_addr(int socket,const SA *grp, socklen_t grplen)
{
  //  INFO("sock_set_multicast_from_addr");  
  return mcast_join(socket,grp,grplen,NULL, 0);
}

extern int sock_set_multicast(int socket,const char* grp_host)
{
  struct addrinfo *ai;
  int ret=0;
  
  INFO("sock_set_multicast");
  //Want to get an addr for a multicast group associated with a port
  ai=sock_get_addrinfo(grp_host,NULL, SOCK_DGRAM,AF_UNSPEC);

  if(ai==NULL)
    {
      ERR("Unable to set up fd %d to multicast");
      return (-1);
    }
  
  if(ai->ai_next==NULL)
    //    INFO("====> OK WELL DONE ;-) ");
  
  //NULL => do not know the IP@ of the network interface
  //0 => do not know the index of the network interface
  ret=mcast_join(socket,ai->ai_addr,ai->ai_addrlen,NULL, 0);
  
  //free ai as mcast_join did a copy of the field ai->ai_addr
  freeaddrinfo(ai);
  return ret;
}

/**
 * From Richard Stevens
 *
 */

int sock_get_port(const struct sockaddr *sa)
{
  switch (sa->sa_family) {
  case AF_INET: {
    struct sockaddr_in	*sin = (struct sockaddr_in *) sa;
    return(sin->sin_port);
  }
    
  case AF_INET6: {
    struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *) sa;
    return(sin6->sin6_port);
  }

  }
  
  return(-1);
}

/**
 *
 *
 *
 */
void sock_set_port(struct sockaddr *sa, int port)
{
  switch (sa->sa_family) {
  case AF_INET: {
    struct sockaddr_in	*sin = (struct sockaddr_in *) sa;   
    sin->sin_port = port;
    return;
  }
    
  case AF_INET6: {
    struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *) sa;
    sin6->sin6_port = port;
    return;
  }

  }
  return;
}
/*
void sock_set_wild(struct sockaddr *sa, socklen_t salen)
{
	const void	*wildptr;

	switch (sa->sa_family) {
	case AF_INET: {
		static struct in_addr	in4addr_any;

		in4addr_any.s_addr = htonl(INADDR_ANY);
		wildptr = &in4addr_any;
		break;
	}

#ifdef	IPV6
	case AF_INET6: {
		wildptr = &in6addr_any;
		break;
	}
#endif
	}
	sock_set_addr(sa, wildptr);
    return;
}

void sock_set_addr(struct sockaddr *sa, const void *addr)
{
  switch (sa->sa_family) {
  case AF_INET: {
    struct sockaddr_in	*sin = (struct sockaddr_in *) sa;
    memcpy(&sin->sin_addr, addr, sizeof(struct in_addr));
    return;
  }
    
  case AF_INET6: {
    struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *) sa;   
    memcpy(&sin6->sin6_addr, addr, sizeof(struct in6_addr));
    return;
	}
  }
  return;
}
*/
/**
 * From Richard Stevens a little bit altered
 *
 */
char * sock_ntop(const struct sockaddr *sa)
{

  //  INFO("sock_ntop");

    char        portstr[7];
    static char str[128];       /* Unix domain is largest */
    
    //    INFO("AF_INET:%d ,AF_INET6:%d ,AF_UNIX:%d",AF_INET,AF_INET6,AF_UNIX);
    
    switch (sa->sa_family) {
    case AF_INET: {
      //      INFO("sock_AF_INET");
        struct sockaddr_in  *sin = (struct sockaddr_in *) sa;

        if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
            return(NULL);
        if (ntohs(sin->sin_port) != 0) {
            snprintf(portstr, sizeof(portstr), ":%d", htons(sin->sin_port));
            strcat(str, portstr);
        }
        return(str);
    }

    case AF_INET6: {
      // INFO("sock_AF_INET6 detected");
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) sa;

        if (inet_ntop(AF_INET6, &sin6->sin6_addr, str, sizeof(str)) == NULL)
            return(NULL);
        if (ntohs(sin6->sin6_port) != 0) {
            snprintf(portstr, sizeof(portstr), ".%d", htons(sin6->sin6_port));
            strcat(str, portstr);
        }
        return(str);
    }

    default:
        snprintf(str, sizeof(str), "sock_ntop: unknown AF_xxx: %d",
                 sa->sa_family);
        return(str);
    }
    return (NULL);
}



/*
  struct group_req {
     unsigned int gr_interface; // interface index, or 0
struct sockaddr_storage gr_group;  // IPv4 or IPv6 multicast addr 
  }
*/

int family_to_level(int family)
{
	switch (family) {
	case AF_INET:
		return IPPROTO_IP;
	case AF_INET6:
		return IPPROTO_IPV6;
	default:
		return -1;
	}
}

/*
int mcast_join(int sockfd,const struct sockaddr *sa,socklen_t salen,
               const char *ifname,u_int ifindex)
{   

  struct ip_mreq    mreq;
  struct ifreq      ifreq;

  memcpy(&mreq.imr_multiaddr,&((struct sockaddr_in *)sa)->sin_addr,sizeof(struct in_addr));


  if(ifindex > 0)
  {
     if(if_indextoname(ifindex,ifreq.ifr_name) == NULL) {
      return(-1);
     }
     goto doioctl;
  }else if(ifname != NULL) 
  {
     strncpy(ifreq.ifr_name,ifname,IFNAMSIZ);
 
     doioctl:
         if(ioctl(sockfd,SIOCGIFADDR,&ifreq) < 0)
             return(-1);
     memcpy(&mreq.imr_interface,&((struct sockaddr_in *)
          &ifreq.ifr_addr)->sin_addr,sizeof(struct in_addr));
  }else
     mreq.imr_interface.s_addr = htonl(INADDR_ANY);
 
  return(setsockopt(sockfd,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq)));
}
*/

int mcast_join(int sockfd, const SA *grp, socklen_t grplen,
		   const char *ifname, u_int ifindex)
{
	#ifdef MCAST_JOIN_GROUP
	struct group_req req;
	if (ifindex > 0) {
		req.gr_interface = ifindex;
	} else if (ifname != NULL) {
		if ( (req.gr_interface = if_nametoindex(ifname)) == 0) {
			errno = ENXIO;	/* i/f name not found */
			return(-1);
		}
	} else
		req.gr_interface = 0;
	if (grplen > sizeof(req.gr_group)) {
		errno = EINVAL;
		return -1;
	}
	memcpy(&req.gr_group, grp, grplen);
	return (setsockopt(sockfd, family_to_level(grp->sa_family),
			MCAST_JOIN_GROUP, &req, sizeof(req)));
	#else
	/* include mcast_join2 */
		switch (grp->sa_family) {
		case AF_INET: {
			struct ip_mreq		mreq;
			struct ifreq		ifreq;

			memcpy(&mreq.imr_multiaddr,
				   &((const struct sockaddr_in *) grp)->sin_addr,
				   sizeof(struct in_addr));

			if (ifindex > 0) {
				if (if_indextoname(ifindex, ifreq.ifr_name) == NULL) {
					errno = ENXIO;	/* i/f index not found */
					return(-1);
				}
				goto doioctl;
			} else if (ifname != NULL) {
				strncpy(ifreq.ifr_name, ifname, IFNAMSIZ);
	doioctl:
				if (ioctl(sockfd, SIOCGIFADDR, &ifreq) < 0)
					return(-1);
				memcpy(&mreq.imr_interface,
					   &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr,
					   sizeof(struct in_addr));
			} else
				mreq.imr_interface.s_addr = htonl(INADDR_ANY);

			return(setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
							  &mreq, sizeof(mreq)));
		}
	/* end mcast_join2 */

	/* include mcast_join3 */
	#ifdef	IPV6
	#ifndef	IPV6_JOIN_GROUP		/* APIv0 compatibility */
	#define	IPV6_JOIN_GROUP		IPV6_ADD_MEMBERSHIP
	#endif
		case AF_INET6: {
			struct ipv6_mreq	mreq6;

			memcpy(&mreq6.ipv6mr_multiaddr,
				   &((const struct sockaddr_in6 *) grp)->sin6_addr,
				   sizeof(struct in6_addr));

			if (ifindex > 0) {
				mreq6.ipv6mr_interface = ifindex;
			} else if (ifname != NULL) {
				if ( (mreq6.ipv6mr_interface = if_nametoindex(ifname)) == 0) {
					errno = ENXIO;	/* i/f name not found */
					return(-1);
				}
			} else
				mreq6.ipv6mr_interface = 0;

			return(setsockopt(sockfd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
							  &mreq6, sizeof(mreq6)));
		}
	#endif

		default:
			errno = EAFNOSUPPORT;
			return(-1);
		}
	#endif
	}
	/* end mcast_join3 */




