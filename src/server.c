#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h> //memset
#include <pthread.h>
#include <sys/select.h> // FD_SET
#include <errno.h>      // for variable errno

#include <z2z_socket.h> // from z2z
#include <z2z_connection.h> // GW_CONN_PTR
#include <z2z_log.h>
#include <z2z_dlist.h>
#include <z2z_tpool.h>
#include <z2z_server.h>     // GW_SERV_PARAM_PTR
#include <z2z_lock.h>
#include <z2z_pipe.h>

/**
 * server thread pool and statistics
 */
//minimum of server thread
static unsigned int gw_server_thread_min = 1;
//maximum of server thread
static unsigned int gw_server_thread_max = 5;
//current number of server thread started
static volatile unsigned int gw_server_thread_count = 0;
//lock for the	gateway
static pthread_mutex_t gw_lock = PTHREAD_MUTEX_INITIALIZER;

void gw_server_tcp_handle_request(void *arg);
void gw_server_udp_handle_request(void *arg);


/**
 *
 */
extern int server_lock(GW_SERV_PARAM_PTR sc) {
  return _lock(&sc->_lock);
}

/**
 *
 */
extern int server_unlock(GW_SERV_PARAM_PTR sc){
  return _unlock(&sc->_lock);
}

/**
 *
 */
extern int server_destroy_lock(GW_SERV_PARAM_PTR sc){
  return _destroy_lock(&sc->_lock);
}

//set  specific tcp connect handler when a connect is detected
extern int 
server_set_left_tcp_connect_hdlr(GW_SERV_PARAM_PTR sc, void (*handler) ()){
  //server_lock(sc);
  sc->tcp_connect_handler = handler;
  //server_unlock(sc);
  return 0;
}

//Set a specific tcp connect handler when a connect is detected
extern int 
server_set_left_tcp_close_hdlr(GW_SERV_PARAM_PTR sc, void (*handler) (void *arg)){
  //server_lock(sc);
  sc->tcp_close_handler = handler;
  //server_unlock(sc);
  return 0;
}

/**
 *
 */
extern int 
server_set_left_host(GW_SERV_PARAM_PTR sc, char *left_host, int left_port, int left_type)
{
  //server_lock(sc);
  
  sc->left_host = left_host;
  sc->left_port = left_port;
  sc->left_socktype = left_type;
  
  switch (left_type) {
  case SOCK_STREAM:
    sc->left_receive_handler = gw_conn_tcp_recv;
    sc->connection_handler = gw_handle_new_tcp_connection;
    sc->_request_handler= gw_server_tcp_handle_request;
    if ((sc->gw_left_srvsock = sock_create_srvsock_on_specific_interface(left_host, left_port, left_type)) < 0)
      exit(-1);
    break;
  case SOCK_DGRAM:
    sc->left_receive_handler = gw_conn_udp_recv;
    sc->connection_handler = gw_handle_new_udp_connection;
    sc->_request_handler= gw_server_udp_handle_request;
    if ((sc->gw_left_srvsock = sock_create_srvsock_on_specific_interface(left_host, left_port, left_type)) < 0)
      exit(-1);
    sock_set_noblock(sc->gw_left_srvsock);
    break;
  }
  //server_unlock(sc);
  return 0;
}

extern int 
server_set_conn_not_alive(GW_CONN_LIST_ELT_PTR elt)
{
  elt->is_alive = CONN_NOT_ALIVE;
  return 1;
}

extern int 
server_set_left_host_to_multicast(GW_SERV_PARAM_PTR sc)
{
  sc->is_left_multicast = 1;
  return sock_set_multicast(sc->gw_left_srvsock, sc->left_host);
}

/**
 * Init a new connection element but DO NOT CREATE EFFECTIVELY THE CONNECTION !! => 13/11/08
 */
extern GW_CONN_LIST_ELT_PTR 
server_init_new_conn_elt(int id, char *host, int port, int type, int multicast)
{
  GW_CONN_LIST_ELT_PTR elt=NULL;
  if ((elt = (GW_CONN_LIST_ELT_PTR) malloc(sizeof(GW_CONN_LIST_ELT))) == NULL) {
    ERR("Failed to allocate server thread structure");
    return NULL;
  }
  
  elt->id = id;
  elt->conn=NULL;
  
  if(type==SOCK_DGRAM){
    elt->is_alive = CONN_NOT_ALIVE; 
    elt->receive_handler=gw_conn_udp_recv;
  }else{
    elt->is_alive = CONN_UNKNOWN;
    elt->receive_handler=gw_conn_tcp_recv;
  }

  elt->is_open = CONN_CLOSE;
  elt->host = host;
  elt->port = port;
  elt->socktype = type;
  elt->is_multicast = multicast;
  elt->request_handler=NULL;
  INIT_LIST_HEAD(&(elt->list));
  return elt;
}

/**
 * Create a runtime configuration structure to initialize one server thread
 *
 *@param type, define the type of the service SOCK_STREAM for tcp or SOCK_DGRAM for udp
 *@param port, define the port to listen to
 *@return GW_SERV_PARAM_PTR return the runtime configuration structure to initialize one server thread
 */
extern GW_SERV_PARAM_PTR 
server_create_default_gateway()
{
  GW_SERV_PARAM_PTR sc=NULL;
  
  //Lock the gateway before doing any change
  _lock(&gw_lock);

  if (gw_server_thread_count >= gw_server_thread_max) {
    WARN("Thread count has reached maximum");
    (void) _unlock(&gw_lock);
    return NULL;
  }
  //allocate the server thread structure
  if ((sc = (GW_SERV_PARAM_PTR) malloc(sizeof(GW_SERV_PARAM))) == NULL) {
    ERR("Failed to allocate server thread structure");
    (void) _unlock(&gw_lock);
    return NULL;
  }
  
  //Initialize the mutex
  if ((pthread_mutex_init(&(sc->_lock), NULL)) != 0) {
    ERR("Failed to initialize the lock of the server thread structure");
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }

  //Initialize the mutex
  if ((pthread_mutex_init(&(sc->gw_pipe_list_lock), NULL)) != 0) {
    ERR("Failed to initialize the lock of the List of active connections");
    (void) _destroy_lock(&(sc->_lock));
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }

  //Initialize the mutex
  if ((pthread_mutex_init(&(sc->gw_fd_list_lock), NULL)) != 0) {
    ERR("Failed to initialize the lock of the List of active fd list");
    (void) _destroy_lock(&(sc->_lock));
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }

  //Initialize the mutex
  if ((pthread_mutex_init(&(sc->gw_conn_out_list_alive_lock), NULL)) != 0) {
    ERR("Failed to initialize the lock of the List of active connections");
    (void) _destroy_lock(&sc->_lock);
    (void) _destroy_lock(&sc->gw_fd_list_lock);
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }

  //Lock the server thread structure before doing any change
  if (server_lock(sc) != 0) {
    ERR("Failed to lock the server thread structure");
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }
  //Create thread pool
  // tpool_t * tpool_init(int num_worker_threads, int max_queue_size, int do_not_block_when_full)
  if ((sc->gw_thread_pool = tpool_init(GW_THR_CLIENT_MAX, GW_THR_CLIENT_QUEUE_MAX, GW_THR_CLIENT_BLCK)) == NULL) {
    ERR("Failed to initialize the thread pool belonging to the server thread structure");
    server_unlock(sc);
    (void) _destroy_lock(&(sc->_lock));
    free(sc);
    (void) _unlock(&gw_lock);
    return NULL;
  }
  //Initialization

  //Create a new head
  INIT_LIST_HEAD(&sc->gw_pipe_list);
  INIT_LIST_HEAD(&sc->conn_out_list_alive);
  INIT_LIST_HEAD(&sc->gw_fd_list);

  sc->gw_pipe_list_size = 0;
  sc->gw_pipe_maxid = 0;
  sc->gw_pipe_count = 0;
  sc->gw_pipe_max = 0;
  sc->gw_thread_id = 0;
  sc->left_host = NULL;
  sc->left_port = 0;
  sc->left_socktype = 0;
  
  server_unlock(sc);
  _unlock(&gw_lock);

  return sc;
}

/**
 * Enable the customization of the runtime server thread configuration structure
 * In particular, set a custom connection handler that is called each time a request is received
 * before the dispatching to a thread pool.
 *
 * @param sc, the configuration sucture
 * @param hdlr, a function defining the connection handler
 * @return TRUE if success, otherwise FALSE
 */
extern int 
set_server_conn_hdlr(GW_SERV_PARAM_PTR sc, GW_CONN_PTR(*hdlr) (int, void *(*gw_recv_func) (void *)))
{
  if ((hdlr == NULL) || (sc == NULL)) {
    ERR("Failed to customize the server thread structure, unable to set the connection handler");
    return (-1);
  }

  sc->connection_handler = hdlr;

  return 0;
}

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
extern int 
server_set_left_recv_hdlr(GW_SERV_PARAM_PTR sc, void *(*hdlr) (void *))
{
  if ((hdlr == NULL) || (sc == NULL)) {
    ERR("Failed to customize the server thread structure, unable to set the receive handler");
    return (-1);
  }

  sc->left_receive_handler = hdlr;

  return 0;
}

/**
 * Enable the customization of the runtime server thread configuration structure
 * In particular, set a custom request handler that is called each time a request is received
 * The handler has access to the receive buffer containing the request
 *
 * @param sc, the configuration sucture
 * @param hdlr, handler to manage the request
 * @return TRUE if success, otherwise FALSE
 */
extern int 
server_set_left_rqst_hdlr(GW_SERV_PARAM_PTR sc, void (*hdlr) (void *)) {

  sc->left_request_handler = hdlr;

  return 0;
}

extern int 
server_set_conn_elt_rqst_hdlr(GW_CONN_LIST_ELT_PTR elt, void (*hdlr) (void *)) {
  if ((hdlr == NULL) || (elt == NULL)) {
    ERR("Failed to customize a connection elt, unable to set the receive handler");
    return (-1);
  }
  elt->request_handler = hdlr;

  return 0;
}

/** 
 *  The server is listening on a set of sockets
 *  Check this set and update the max fd
 */
int             
server_get_fd_set(GW_SERV_PARAM_PTR gw, fd_set * fds) {
  GW_FD_LIST_ELT_PTR fd_elt=NULL;
  int max=0;
  
  _lock(&gw->gw_fd_list_lock);
  
  list_for_each_entry(fd_elt, &gw->gw_fd_list, list) {
    if(fd_elt->i!=0){
      FD_SET(fd_elt->i, fds);
      if (fd_elt->i > max){
				max = fd_elt->i;
      }
    }
  }
  
  _unlock(&gw->gw_fd_list_lock);

  return (max);
}

int 
server_rm_fd_set(GW_SERV_PARAM_PTR gw, int fd){
  
  struct list_head *pos=NULL;
  struct list_head *q=NULL;
  GW_FD_LIST_ELT_PTR tmp=NULL;
  
  _lock(&gw->gw_fd_list_lock);
  
  list_for_each_safe(pos, q, &gw->gw_fd_list) {
    tmp = list_entry(pos, GW_FD_LIST_ELT, list);
    if(tmp->i==fd){
      list_del(pos); 
      free(tmp);
      tmp=NULL;
    }
  }
  
  _unlock(&gw->gw_fd_list_lock);
  return EXIT_SUCCESS;
}

int
server_add_fd_set(GW_SERV_PARAM_PTR gw, int fd){
  _lock(&gw->gw_fd_list_lock);
  GW_FD_LIST_ELT_PTR fd_elt=NULL;
  fd_elt = (GW_FD_LIST_ELT_PTR)malloc(sizeof (GW_FD_LIST_ELT));
  fd_elt->i=fd;
  list_add(&fd_elt->list,&gw->gw_fd_list);
  _unlock(&gw->gw_fd_list_lock);
  return EXIT_SUCCESS;
}


GW_PIPE_LIST_ELT_PTR 
server_is_pipe_ready(struct list_head *gw_pipe_list,pthread_mutex_t *gw_pipe_list_lock ,int fd) {
  ERR("server_is_pipe_ready starting");
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;
  GW_CONN_LIST_ELT_PTR conn_elt=NULL;
  struct list_head *pos=NULL;
  struct list_head *q=NULL;
  
  _lock(gw_pipe_list_lock);
  
  list_for_each_entry(pipe_elt, gw_pipe_list,list) {
    //_lock(&pipe_elt->_lock);
    //    list_for_each_entry(conn_elt, &pipe_elt->conn_out_list,list) {
    list_for_each_safe(pos,q,&pipe_elt->conn_out_list){
      conn_elt=list_entry(pos,GW_CONN_LIST_ELT,list);
      if ((conn_elt->conn != NULL) && (conn_elt->conn->sock == fd)) {
	//if (pthread_mutex_trylock(&(conn_elt->conn->lock)) == EBUSY)
	//  return NULL;
	INFO("a precedent pipe has been found, launch a new server thread for its output connections : %d", fd);
	assert(conn_elt->receive_handler!=NULL);
	conn_elt->receive_handler(conn_elt->conn);
	pipe_elt->conn_ready = conn_elt;
	list_del(&pipe_elt->list); //Then ?? Remove the pipe_elt
	pipe_elt->list.next=NULL;
	//_unlock(&conn_elt->conn->lock);
	//_unlock(&pipe_elt->_lock);
	_unlock(gw_pipe_list_lock);
	WARN("server_is_pipe_ready ending");
	return pipe_elt;
      }
    }
    //_unlock(&pipe_elt->_lock);
  }
  
  _unlock(gw_pipe_list_lock);
  
  WARN("server_is_pipe_ready ending");
  return NULL;
}

/**
 * Wrapper function to gw_server_thread_create,
 * enable to launch the runtime server thread from the given configuration stucture
 *
 * @param sp, the configuration structure or also called 's'erver 'p'arameter
 * @return int, the runtime thread id is returned if success, otherwise return FALSE
 */
extern int      
gw_create_new_server_thread(GW_SERV_PARAM_PTR sp) {
  return gw_server_thread_create(sp, gw_server_dispatcher, (void *) sp);
}

/**
 *
 *
 */
extern void 
gw_server_udp_handle_request(void *arg) {
  ERR("gw_server_udp_handler_request starting");
  GW_PIPE_LIST_ELT_PTR pipe_elt = (GW_PIPE_LIST_ELT_PTR) arg;
  GW_SERV_PARAM_PTR sp = (GW_SERV_PARAM_PTR) pipe_elt->gw;

  sp->left_request_handler((void *) pipe_elt);
 
  //In case we are receiving an udp request on the left side,
  //If we perform a continuation, ie a resp_send_sync then we have an issue here.
  //As we free data before the end of the continuation. 
  // free_pipe_0(pipe_elt);
 
  WARN("gw_server_udp_handler_request ending");
}

/* Thread stop and delete it */
void
delete_thread (void* handle){
  int ret;
  pthread_t* p_thread = (pthread_t*)handle;

  if (p_thread){
    if((ret=pthread_join (*p_thread, NULL))!=0){
      ERR("Failed to join recv thread");
      switch (ret) {
      case ESRCH:
	WARN("No thread could be found corresponding to that specified by the given thread ID");
	break;
      case EDEADLK:
	WARN("A deadlock would occur if the thread blocked waiting for mutex");
	break;
      case EINVAL:
	WARN("The implementation has detected that the value specified by thread does not refer to a joinable thread.");
	break;
      }
      //exit(EXIT_FAILURE); 
    }
    pthread_detach (*p_thread);
  }
  free (p_thread);
  p_thread = NULL;
  return;
}

/**
 *
 *
 */
void 
gw_server_tcp_handle_request(void *arg) {
  ERR("gw_server_tcp_handler_request starting");
    
  pthread_t* ptid;

  GW_PIPE_LIST_ELT_PTR pipe_elt = (GW_PIPE_LIST_ELT_PTR) arg;
  GW_SERV_PARAM_PTR sp = (GW_SERV_PARAM_PTR) pipe_elt->gw;

  GW_CONN_PTR     conn = pipe_elt->conn_in;

  //  pthread_mutex_lock(&sp->_lock);
  if(sp->left_socktype == SOCK_STREAM) {
    //Check if the MACRO has been used in the main.c of the gateway ! !
    assert(sp->tcp_connect_handler != NULL);
    sp->tcp_connect_handler((void *) pipe_elt);
  }
  
  //pthread_mutex_unlock(&sp->_lock);
  
  //Lock the gateway before doing an accept on the servsock THAT IS SHARED AMONG THREADS !
  //if (pthread_mutex_lock(&gw_server_thread_lock) != 0) {
  //ERR("Failed to lock server thread pool");
  //exit(EXIT_FAILURE);
  //}
  
  //Create a control lock on the connection
  Z2Z_LOCK_PTR lock = gw_create_tcp_lock();
  assert(lock != NULL);
  assert(conn->z2z_lock == NULL);
  
  ptid = (pthread_t*)malloc(sizeof (pthread_t));
  if (!ptid)
    exit(EXIT_FAILURE);
  
  //Set it to the connection
  lock_conn(conn);
  conn->z2z_lock = lock;
  unlock_conn(conn);

  //Create a new thread for receiving data
  pthread_create(ptid, NULL, (sp->left_receive_handler), (void *) conn);
  ERR("=> Thread %u is created to receive data",(unsigned int)*ptid);
  
  while (1) {
    pthread_mutex_lock(&(lock->done_mutex));  

    while ((lock->active) == 0) {
      pthread_cond_wait(&(lock->is_active), &(lock->done_mutex));
    }

    //INFO("Hold the lock");
    if (gw_conn_get_status(conn) == CONN_EOF)
      break;

    //Sleep and wait a signal from the receive thread, release the lock
    //INFO(" WORKING WITH DATA");
    sp->left_request_handler((void *) pipe_elt);

    INFO("conn status: %d", gw_conn_get_status(conn));
    lock->active = 0;
    pthread_cond_signal(&(lock->not_active));
    pthread_mutex_unlock(&(lock->done_mutex));
 }
  //INFO("conn status: %d", gw_conn_get_status(conn)); //bugs
 pthread_mutex_unlock(&lock->done_mutex);
 
 //  WARN("CONN_SOCK: %d CONN_STATUS:%d",conn->sock,conn->status);
 WARN("gw_server_handle_request ending");
 delete_thread(ptid);
 sp->tcp_close_handler((void *) pipe_elt);
 free_pipe_0(pipe_elt);
}

/**
 *  Internal dispatcher, dispatch the receiving requests to a thread pool to process in parallel the requests
 *
 *  @param arg, a pointer to an allocated GW_SERV_PARAM structure casted in a void pointer
 *  @return NULL, always return NULL
 */
extern void*
gw_server_dispatcher(void *arg) {
  INFO("gw_server_dispatcher");
  GW_SERV_PARAM_PTR sp = (GW_SERV_PARAM_PTR) arg;
  int             max; //Highest file descriptor, needed for select()
  int             ret;//Number of sockets ready for reading
  int             i;
  fd_set          rfds;//Set of file descriptor to listen
  struct timeval  timeout;
  int             srvsock;
  GW_PIPE_LIST_ELT_PTR pipe_elt = NULL;
  
  srvsock = sp->gw_left_srvsock;
  
  while(sp->gw_state == GW_THR_RUNNING) {
    
    //FD_ZERO() clears out the fd_set called socks,so that
    //it doesn 't contain any file descriptors.
    FD_ZERO(&rfds);
    
    //FD_SET() adds the file descriptor "sock" to the fd_set, //so that select() will return if a connection comes in
    //on that socket(which means we need to do accept()
    //sock_getfdset
    //FD_SET(srvsock, &rfds);
    //Update 26 / 04 / 08 now we support multi sock listening
    
    //Add lock 13/04/09
    
    //    _lock(&sp->gw_pipe_list_lock);
    max = server_get_fd_set(sp, &rfds);
    //_unlock(&sp->gw_pipe_list_lock);
    
    FD_SET(srvsock, &rfds);
    //work better id the server sock is included;-)
    timeout.tv_sec = 0;
    timeout.tv_usec = 1;

    //Since we start with only one socket, the listening socket,
    //it is the highest socket so far.

    //!!Perhaps better to use a fonction to determine the max fd
    //!!At this time we use only one socket for one protocol
    //!!To change in order for the gateway to perform simultaneous traduction

    //Update 26 / 04 / 08 now we support multisocket listening
    if(srvsock > max)
      max = srvsock;

    ret = select(max + 1, &rfds, NULL, NULL, &timeout);

    //Select() returns the number of sockets that are readable
    if(ret == -1) {
      if (errno == EINTR)
	continue;
      ERR("select error: %s", strerror(errno));
    } else if       (ret == 0) {
      //Nothing ready to read, just show that we 're alive
      //INFO("Dispatcher alive.");
      continue;
    }
    //Run through the existing connections looking for data to be read
    for(i = 0; i <= max; i++) {
      if (FD_ISSET(i, &rfds)) {
				//INFO("Input detected from fd: %d", i);
				if (i == srvsock) {
					
	  ERR("Receive request on main thread udp/tcp starting");
	  if (sock_is_a_socket(i) && sock_can_read(i, 5)) {
	    pipe_elt = pipe_new(sp, sp->connection_handler(srvsock, sp->left_receive_handler), NULL);
	    
	    //We have a new connection coming in !
	    //tpool_add_work(sp->gw_thread_pool, sp->left_request_handler, (void *) pipe_elt);
	    tpool_add_work(sp->gw_thread_pool, sp->_request_handler, (void *) pipe_elt);
	    
	    //Create a new pipe as we now have the input connection
	    // Output connections will be set later;-)
	    // pipe_elt = server_new_pipe(sp, sp->connection_handler(srvsock, sp->left_receive_handler), NULL);
	    //-> 18/03/09
	    //pipe_update_ref(pipe_elt);
	    WARN("Receive request on main thread udp/tcp ending");
	    //<---- end 16/03/09
	  }//
	}//
	else {
	  INFO("Input detected from fd= %d != from fd-server: %d", i, srvsock);
	  pipe_elt = server_is_pipe_ready(&sp->gw_pipe_list, &sp->gw_pipe_list_lock,i);
	  if (pipe_elt == NULL)
	    continue;
	  //pipe_elt has been deleted since by another thread
	  //pipe_update_ref(pipe_elt);
	  tpool_add_work(sp->gw_thread_pool, pipe_elt->conn_ready->request_handler, (void *) pipe_elt);
	}
      }
      else{
	//end if Detected input from other fd
	INFO("Nothing from fd : %d", i);
      }
    }
  }
  INFO("Server thread id:%i launched on port:%i fd:%i is dying", sp->gw_thread_id, sp->left_port, sp->gw_left_srvsock);
  return (NULL);
} //End



  /**
   * Enable to launch the runtime server thread from the given configuration stucture
   * Enable to specify a custum dispatcher that will replace the internal dispatcher called 'gw_server_dispatcher'
   * @param sp, the configuration structure or also called 's'erver 'p'arameter
   * @param dispatcher, the customize dispatcher function
   * @return int, the runtime thread id is returned if success, otherwise return FALSE
   */
int 
gw_server_thread_create(GW_SERV_PARAM_PTR sp, void *(*dispatcher) (void *), void *arg) {
  int ret;
  
  if(sp == NULL) {
    ERR("Failed to create a new server thread, its configuration structure is NULL");
    return (-1);
  }
				
  //Lock the gateway before doing any change
  if (pthread_mutex_lock(&gw_lock) != 0) {
    ERR("Failed to lock server thread pool");
    return (-1);
  }
  
  //server_lock(sp);
  
  sp->gw_state = GW_THR_RUNNING;

  if (((sp->gw_thread_id) != 0)) {
    WARN("Try to allocate a server thread that already exists !!");
    (void) pthread_mutex_unlock(&gw_lock);
    (void) pthread_mutex_unlock(&(sp->_lock));
    return (-1);
  }
  if ((ret = pthread_create(&(sp->gw_thread), NULL, dispatcher, arg)) != 0) {
    ERR("Failed to create server thread: %s", strerror(ret));
    (void) pthread_mutex_unlock(&gw_lock);
    return (-1);
  }
  sp->gw_thread_id = gw_server_thread_count++;

  //server_unlock(sp);
  (void) pthread_mutex_unlock(&gw_lock);
  return (sp->gw_thread_id);
}

extern int      
server_save_conn_alive(GW_SERV_PARAM_PTR sp, GW_CONN_LIST_ELT_PTR elt) {
  //WARN("Save the output connection");
  assert(elt!=NULL);
  elt->is_alive=CONN_SAVED;
  INIT_LIST_HEAD(&(elt->list));
  list_add(&(elt->list), &(sp->conn_out_list_alive));
  return EXIT_SUCCESS;
}

/**
 * Totally updated 16/03/09
 * 21 oct 08 new free_pipe function
 * Free one pipe element !!!
 */
extern int      
free_pipe_0(GW_PIPE_LIST_ELT_PTR elt) {
  INFO("Free pipe");
  
  assert(elt != NULL);
  struct list_head *pos, *q;
  GW_CONN_LIST_ELT_PTR tmp = NULL;
  
  _lock(&elt->_lock);
  //GW_SERV_PARAM_PTR sp = (GW_SERV_PARAM_PTR) elt->gw;
  
   //Free conn_in
  gw_conn_free(elt->conn_in);
  elt->conn_in = NULL;
  
  //Free conn_out only if the runtime have opened output connections
  list_for_each_safe(pos, q, &(elt->conn_out_list)) {
    tmp = list_entry(pos, GW_CONN_LIST_ELT, list);
    list_del(pos); 
    if (tmp->host != NULL)
      free(tmp->host);
    tmp->host = NULL;
    gw_conn_free(tmp->conn);
    free(tmp);
    tmp = NULL;
  }
  elt->conn_ready = NULL;
  _unlock(&elt->_lock);
  free(elt);
  INFO("Pipe Freed");
  return EXIT_SUCCESS;
}

/**
 *
 *
 * TO BE DONE
 */
int gw_server_thread_kill(int thrid) {
  
  ERR("Threadk Kill");

  return 0;
}
