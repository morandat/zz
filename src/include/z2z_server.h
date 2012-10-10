#ifndef _Z2Z_SERVER_H_
#define _Z2Z_SERVER_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "z2z_connection.h"
#include "z2z_tpool.h"
#include "z2z_dlist.h"


#define MYPORT 33000               // Bordeaux postal code, default port to use ;-)

// server thread states 
#define GW_THR_UNKNOWN_STATE	0		// State of the server thread
#define GW_THR_INITIALIZING	1			// State of the server thread
#define GW_THR_RUNNING		2				// State of the server thread
#define GW_THR_SHUTTING_DOWN	3  	// State of the server thread
#define GW_THR_RESTARTING	4  			// State of the server thread
#define GW_THR_CLIENT_MAX       10// Max client thread per server thread
#define GW_THR_CLIENT_QUEUE_MAX 20// Max queue size of the pool server thread
#define GW_THR_CLIENT_BLCK      1 // Block the client when the pool server thread is full
#define GW_PIPE_MAX	        		100 // Max of ?, I forgot !!   
																	
//Few months later: now I use it to limit the number of simultaneous pipe to use
// by the main server thread

typedef struct gw_serv gw_serv_t;
typedef struct gw_serv GW_SERV_PARAM;
typedef gw_serv_t*     GW_SERV_PARAM_PTR;

enum gw_conn_elt_alive {
  CONN_NOT_ALIVE =0,
  CONN_KEEP_ALIVE=1,
  CONN_SAVED=2
  };
typedef enum gw_conn_elt_alive CONN_ALIVE;

//Connection element for one protocol on the right side
struct gw_conn_list_elt {
  struct list_head list;
  char* host;
  int 	id; // Proto id ?
  int   port;
  int   socktype; //TCP or UDP ?
  int   is_multicast;
  CONN_ALIVE is_alive;
  CONN_STATE is_open;
  void*  (*receive_handler)(void*); 
  void (*request_handler)(void*);
  GW_CONN_PTR conn;
}; 
typedef struct gw_conn_list_elt GW_CONN_LIST_ELT;
typedef GW_CONN_LIST_ELT*     	GW_CONN_LIST_ELT_PTR;

struct gw_fd_list_elt {
  struct list_head list;
  int i;
};
typedef struct gw_fd_list_elt GW_FD_LIST_ELT;
typedef GW_FD_LIST_ELT*       GW_FD_LIST_ELT_PTR;

//Pipe element to be included in pipe_list
struct gw_pipe_list_elt {
  struct list_head list;
  GW_SERV_PARAM_PTR gw;
  GW_CONN_PTR conn_in;
  pthread_mutex_t  _lock; //Added 13/04/09    
  GW_CONN_LIST_ELT_PTR conn_ready; // A pointer on active connection among conn_out_list 
  struct list_head conn_out_list; // List of out connection element for one input connection ; <one to many>
};

typedef struct gw_pipe_list_elt GW_PIPE_LIST_ELT;
typedef struct gw_pipe_list_elt* GW_PIPE_LIST_ELT_PTR;

struct gw_serv {
  
  //Id of the master thread
  int              gw_thread_id;   
  //State of the server thread
  int              gw_state; 
  //Current socket
  int              gw_left_srvsock; 
  
  //Pointer to the current thread
  pthread_t        gw_thread;
  
  //Added 13/04/09    
  pthread_mutex_t  gw_pipe_list_lock; 
  
  //List of active pipes for continuations
  struct list_head gw_pipe_list;     
  
  //Added 13/04/09    
  pthread_mutex_t  gw_fd_list_lock; 
  
  //List of file descriptor to listen to => Added  18 Avril 2009 to avoid deadlock
  struct list_head gw_fd_list;
  
  //List of input connection : Not used anymore I think (18 Avril 2009)
  //  struct list_head gw_conn_in;
  
  //Lock the structure
  pthread_mutex_t  _lock;    

  //actives pipes are pipes with a conn_id included in the fd set used by the main thread
  
  //----> Not used yet <--------
  int              gw_pipe_list_size;
  int              gw_pipe_maxid;
  int              gw_pipe_count;
  int              gw_pipe_max;
  //----> Not used yet <--------
  
  TPOOL_PTR        gw_thread_pool; //Pool of thread
  
  pthread_mutex_t  gw_conn_out_list_alive_lock; //Added 13/04/09    

  //----> Added 15/03/09 <-----
  struct list_head conn_out_list_alive; //List of active alive tcp connections 
  
  //Left side
  //Receive handler from the left side of the gateway
  
  GW_CONN_PTR (*connection_handler)(int,void*(*gw_recv_func)(void*));
  void *(*left_receive_handler)(void*);
  void (*_request_handler)(void*);
  void (*left_request_handler)(void*);
  //  t_conn_hdlr tcp;
  void (*tcp_connect_handler)(void*);
  void (*tcp_close_handler)(void*);
  char* left_host;
  int   left_port;
  int   left_socktype;
  int   is_left_multicast;
  struct addrstorage * left_addr;
};

extern int server_set_left_host(GW_SERV_PARAM_PTR sc, char* remote_host, int remote_port, int type);

extern int server_set_right_host(GW_SERV_PARAM_PTR sc, char* remote_host, int remote_port, int type);

extern GW_SERV_PARAM_PTR server_create_default_gateway();

/**
 * Create a runtime configuration structure to initialize one server thread
 *
 * @param type, define the type of the service SOCK_STREAM for tcp or SOCK_DGRAM for udp
 * @param port, define the port to listen to
 * @return GW_SERV_PARAM_PTR return the runtime configuration structure to initialize one server thread
 */
extern GW_SERV_PARAM_PTR server_default_factory(int type, int port);

/**
 * Wrapper function to gw_server_thread_create, 
 * enable to launch the runtime server thread from the given configuration stucture
 *
 * @param sp, the configuration structure or also called 's'erver 'p'arameter 
 * @return int, the runtime thread id is returned if success, otherwise return FALSE 
 */
extern int gw_create_new_server_thread(GW_SERV_PARAM_PTR sp);

/**
 * Enable the customization of the runtime server thread configuration structure
 * In particular, set a custom connection handler that is called each time a request is received
 * before the dispatching to a thread pool.
 *
 * @param sc, the configuration sucture
 * @param hdlr, a function defining the connection handler
 * @return TRUE if success, otherwise FALSE
 */
extern int set_server_conn_hdlr(GW_SERV_PARAM_PTR sc, GW_CONN_PTR (*hdlr)(int,void* (*gw_recv_func)(void*)));

/**
 * Enable the customization of the runtime server thread configuration structure
 * In particular, set a custom receive handler that is called each time a request is received
 * The handler reads from the server thread socket if udp or from a client socket if tcp 
 * The handler fills in the receive buffer of the structure pointed by the GW_CONN_PTR 
 *
 * @param sc, the configuration sucture
 * @param hdlr, handler to read from a socket and fill a receive buffer
 * @return TRUE if success, otherwise FALSE
 */
extern int set_right_recv_hdlr(GW_SERV_PARAM_PTR sc,void* (*hdlr)(void*));

extern int server_set_left_recv_hdlr(GW_SERV_PARAM_PTR sc,void* (*hdlr)(void*));

/**
 * Enable the customization of the runtime server thread configuration structure
 * In particular, set a custom request handler that is called each time a request is received
 * The handler has access to the receive buffer containing the request
 *
 * @param sc, the configuration sucture
 * @param hdlr, handler to manage the request
 * @return TRUE if success, otherwise FALSE
 */
extern int server_set_left_rqst_hdlr(GW_SERV_PARAM_PTR sc,void (*hdlr)(void *));

extern int server_set_right_rqst_hdlr(GW_SERV_PARAM_PTR sc,void (*hdlr)(void *));

/**
 *  Internal dispatcher, dispatch the receiving requests to a thread pool to process in parallel the requests
 *  
 *  @param arg, a pointer to an allocated GW_SERV_PARAM structure casted in a void pointer
 *  @return NULL, always return NULL
 */
void* gw_server_dispatcher(void* arg);

/**
 * Enable to launch the runtime server thread from the given configuration stucture
 * Enable to specify a custum dispatcher that will replace the internal dispatcher called 'gw_server_dispatcher'
 * @param sp, the configuration structure or also called 's'erver 'p'arameter 
 * @param dispatcher, the customize dispatcher function 
 */
extern int gw_server_thread_create(GW_SERV_PARAM_PTR sp, void* (*dispatcher)(void*),void* arg);

extern int server_set_left_host_to_multicast(GW_SERV_PARAM_PTR sc);

extern int free_pipe(GW_PIPE_LIST_ELT_PTR pipe);

extern GW_CONN_LIST_ELT_PTR server_init_new_conn_elt(int id, char* host, int port, int type, int multicast);

extern GW_CONN_LIST_ELT_PTR server_add_right_conn_elt_to_pipe(GW_PIPE_LIST_ELT_PTR pipe_elt, int id, const char* host, int port, int type, int multicast,void (*request_handler)(void * arg), void* (*receive)(void *));
//extern int server_set_left_tcp_connect_hdlr(GW_SERV_PARAM_PTR sc, void (*handler)(void * arg),int is_void);

extern int server_set_left_tcp_connect_hdlr(GW_SERV_PARAM_PTR sc, void (*handler)(void * arg));

extern int server_set_left_tcp_close_hdlr(GW_SERV_PARAM_PTR sc, void (*handler)(void * arg));
extern int server_set_conn_elt_recv_hdlr(GW_CONN_LIST_ELT_PTR elt,void* (*hdlr)(void*));
extern int free_pipe_0(GW_PIPE_LIST_ELT_PTR elt);
extern GW_CONN_PTR server_remove_right_conn_elt_from_pipe(int id,GW_PIPE_LIST_ELT_PTR elt);
extern int server_set_delete_flag(GW_CONN_LIST_ELT_PTR elt);
extern void server_garbage_conn_elt(GW_PIPE_LIST_ELT_PTR elt);
extern int server_save_conn_alive(GW_SERV_PARAM_PTR sp, GW_CONN_LIST_ELT_PTR elt);
extern int server_lock(GW_SERV_PARAM_PTR sc);
extern int server_unlock(GW_SERV_PARAM_PTR sc);
extern int server_add_fd_set(GW_SERV_PARAM_PTR gw, int fd);
extern int server_rm_fd_set(GW_SERV_PARAM_PTR gw, int fd);

#endif // _Z2Z_SERVER_H_
