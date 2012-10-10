#ifndef _Z2Z_SOCKET_H_
#define _Z2Z_SOCKET_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netinet/in.h> // for struct sockaddr_in
#include <netdb.h>
#include <net/if.h>
#define	SA	struct sockaddr

/*
#ifndef MCAST_JOIN_GROUP
# define MCAST_JOIN_GROUP 42
# define MCAST_BLOCK_SOURCE 43
# define MCAST_UNBLOCK_SOURCE 44
# define MCAST_LEAVE_GROUP 45
struct group_req
{
	uint32_t gr_interface;
	struct sockaddr_storage gr_group;
};
#endif 
*/


/**
 * Increase the buffer as much as possible for udp socket
 * @param udp_sock An udp socket
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
//static int sock_probe_max_receive_buffer( int udp_sock );

/**
 * Check if data is available, if not, wait timeout seconds for data
 * to be present.
 * @param socket, A socket
 * @param timeout How long to wait before timeout (value in seconds)
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
extern int sock_can_read(int socket, int timeout);

/**
 * Initialization of a <code>sockaddr_in</code> structure 
 * with a dedicated <code>hostname</code> and <code>port</code>
 * if the <code>hostname</code> is set to <code>NULL</code> then <code>INADDR_ANY</code> is used 
 *
 * @param addr, a pointer to a <code>sockaddr_in</code> structure to initialize
 * @param hostname, the string corresponding to the host location
 * @param port, the port to use
 * @return  <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
//extern int sock_init_sockaddr(struct sockaddr_in *addr, const char *hostname, int port);

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
extern int sock_create_valid_socket(const struct addrinfo *addr);

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
extern int sock_init_tcp_connection(const char * remote_host, int remote_port, socklen_t *len);


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
extern int sock_init_udp_connection(const char * remote_host, int remote_port, SA ** sa_ptr,socklen_t *len);

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
extern int sock_init_connection(const char *host, const char *serv, int type);


/**
 * Send one lines of text <code>string<code> on the <code>socket</code> 
 * @param socket the socket to write to
 * @param string The string to write
 * @return The number of bytes sent or -1 if an error occured.
 */
extern int sock_send_string(int socket, const char *string);

/**
 * Close the <code>socket<code>
 * @param socket the socket to close
 * @return Return TRUE if the event occured, otherwise FALSE
 */
extern int sock_close (int socket);

/**
 * Send <code>size</code> bytes from the <code>src</code> to the
 * <code>socket</code> 
 * @param socket the socket to write to
 * @param buffer The buffer to write
 * @param size Number of bytes to send
 * @param timeout Seconds to wait for data to be written
 * @return The number of bytes sent or -1 if an error occured.
 */
extern int sock_tcp_send (int socket, const void *src, size_t size,int timeout);

extern int sock_udp_send (int socket, const void *src, size_t size, const struct sockaddr *dest_addr,socklen_t dest_len,int timeout);
/**
* Check if data can be sent to the <code>socket</code>, if not, wait timeout
* seconds for the <code>socket</code> to be ready.
* @param socket A socket
* @param timeout How long to wait before timeout (value in seconds)
* @return Return TRUE if the event occured, otherwise FALSE.
*/
extern int sock_can_write(int socket, int timeout);

/**
 * Determine if a file descriptor <code>fd</code> is in fact a socket
 *
 * @param fs, A socket
 * @return TRUE if it is a socket, otherwise FALSE
 */
extern int sock_is_a_socket(int socket);

/**
 * Enable nonblocking i|o on the given <code>socket</code>.
 * @param socket A socket
 * @return TRUE if success, otherwise FALSE
 */
extern int sock_set_noblock(int socket);

/**
 * Disable nonblocking i|o on the given <code>socket</code>
 * @param socket, A socket
 * @return TRUE if success, otherwise FALSE
 */
extern int sock_set_block(int socket);

/**
 * Create server socket that listens on one <code>port</code>
 *
 * @param port, port number of the server socket
 * @param type, socket type to use SOCK_STREAM or SOCK_DRGAM is expected
 * @param protocol, specifies a particular protocol to be used with the socket 
 * e.g. IPPROTO_TCP for tcp,  IPPROTO_UDP for udp
 * @return int, the socket file descriptor
 */
extern int sock_create_srvsock(unsigned short port, int type);

extern int sock_create_srvsock_from_service(const char *service, const char *type);

/**
 * Helper function to create a udp server socket listening on a specified port
 *
 * @param port, port number of the server socket
 * @return int, the socket file descriptor
 */
extern int sock_create_udp_srvsock(unsigned short port);

/**
 * Helper function to create a tcp server socket listening on a specified port
 *
 * @param port, port number of the server socket
 * @return int, the socket file descriptor
 */
extern int sock_create_tcp_srvsock(unsigned short port);

extern int sock_create_srvsock_on_specific_interface(const char* remote_host, unsigned short remote_port, int type);

extern int sock_set_multicast_from_addr(int socket,const SA* grp,socklen_t grplen);

extern int sock_set_multicast(int socket,const char* grp_host);

extern char * sock_ntop(const struct sockaddr *sa);

extern int sock_get_port(const struct sockaddr *sa);

extern void sock_set_port(struct sockaddr *sa, int port);

extern int sock_init_connection_from_addrinfo(const struct addrinfo *ai);

extern struct addrinfo *sock_get_addrinfo(const char *remote_host, const char *remote_port, int type, short family);

int mcast_join(int sockfd, const SA *grp, socklen_t grplen, const char *ifname, u_int ifindex);

int sock_read(int socket, void *buffer, int size, int timeout);

#endif // _Z2Z_SOCKET_H_
