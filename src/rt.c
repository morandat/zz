#include <pthread.h>
#include <errno.h>      // for variable errno

#include "z2z_hashtable.h"
#include "z2z_template.h"
#include "z2z_runtime.h"
#include "z2z_log.h"
#include "z2z_string.h"
#include "z2z_dlist.h"
#include "z2z_connection.h"  
#include "z2z_server.h"  
#include "z2z_socket.h"  
#include "z2z_pipe.h"  
#include "hashtable_private.h"

// A private variable
static pthread_key_t pipe_data;

//static pthread_key_t conn_in_data;
//static pthread_key_t conn_out_data;

// A global save state structure as a hashtable
static HASHTABLE_PTR left_hashtable;
static HASHTABLE_PTR right_hashtable;

static int isInitialized = 0;
static pthread_once_t once_block = PTHREAD_ONCE_INIT;
//lock for the left hashtable
static pthread_mutex_t left_hashtable_lock = PTHREAD_MUTEX_INITIALIZER;
//lock for the right hashtable
static pthread_mutex_t right_hashtable_lock = PTHREAD_MUTEX_INITIALIZER;

#define HASHTABLE_SIZE 5 //Initial size of the global hastable

DEFINE_HASHTABLE_INSERT(rt_hashtable_insert,char, struct list_element);
DEFINE_HASHTABLE_SEARCH(rt_hashtable_search,char, LIST_ELT);
DEFINE_HASHTABLE_REMOVE(rt_hashtable_remove,char, struct list_element);
//DEFINE_HASHTABLE_ITERATOR_SEARCH(rt_hashtable_itr_search, char);

static unsigned int rt_hash_from_key(void *ky);
static int rt_equal_keys(void *k1, void *k2);
static void rt_init_routine();
static void* internal_get_private_pipe_data(pthread_key_t key);
static int internal_save_private_pipe_data(void* pipe_info, pthread_key_t key);
static void internal_delete_private_pipe_data(void *pipe_info,pthread_key_t key);
static GW_CONN_LIST_ELT_PTR send_sync(struct protocol *p,char *m, size_t len);
static GW_PIPE_LIST_ELT_PTR set_host(int id,const char* ip, int port, int type, int multicast,  void (*handler)(void *), void* (*receive)(void *));
static GW_CONN_LIST_ELT_PTR rt_get_conn_from_id(int proto_id,int conn_id,struct list_head* conn_elt_list);

/************************************************************************************/


/**
 * Call to this intialization method is a mandatory before 
 * using any methods provided by the rt library
 */
extern int rt_init()
{
  return pthread_once(&once_block, rt_init_routine);
}

/**
 * Internal initialization routine which may be called only once
 */
static void rt_init_routine()
{
  int ret;
  isInitialized=1;
  
  //Initialize private data for a specific threads
  ret=pthread_key_create(&pipe_data, rt_delete_private_pipe_data);
  
  if(ret< 0)
    {
      switch(ret){
      case EAGAIN:
	ERR("The system lacked the necessary resources to create another thread-specific data key",strerror(errno));
	exit(EXIT_FAILURE);
	break;
	
      case ENOMEM:
	ERR("Insufficient memory exists to create a key for a connection data dedicated to a worker thread",strerror(errno));
	exit(EXIT_FAILURE);
	break;
      }
    }

  //Initialize global data shared between all threads
  left_hashtable =
    create_hashtable(HASHTABLE_SIZE, rt_hash_from_key, rt_equal_keys);
  right_hashtable =
    create_hashtable(HASHTABLE_SIZE, rt_hash_from_key, rt_equal_keys);
   
  if (left_hashtable == NULL || right_hashtable == NULL)
    {
      ERR("Failed to initialize the runtime hashtable");
      exit(EXIT_FAILURE);
    }
}

/**
 * Function used to compare two hashtable keys
 *
 * @param k1, key to compare with
 * @param k2, key to compare with
 * @return 1 if equals, O otherwise
 */
static int rt_equal_keys(void *k1, void *k2)
{
  //bugs?
  //ERR("key1: %s <=> key2: %s",(char*)k1,(char*)k2);
 return (!(strcmp((char*)k1,(char*)k2)));
}
    
/**
 * djb2 Alogorithm reported by Dan Bernstein in comp.lang.c
 * 
 * @param ky, an array of data
 * @return a hash value
 */
static unsigned int rt_hash_from_key(void *ky)
{
  register unsigned int hash = 5381;
  int c;
  char* str;

  str=(char*)ky;
  
  while ((c = *str++)) 	
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  return hash;
}

/**
 *
 *
 */
extern int rt_save_private_pipe_data(void* pipe_info)
{
  return internal_save_private_pipe_data(pipe_info,pipe_data);
}

/**
 * Save a spÈcific connection structure for the current thread
 * 
 * @param pipe_info, connection structure to save as private for the current thread
 * @return <code>EXIT_SUCCESS</code> or <code>EXIT_FAILURE</code> if an error occured.
 */
static int internal_save_private_pipe_data(void* pipe_info, pthread_key_t key)
{
  void *tmp;
  int ret;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
    
  if(pipe_info==NULL) {
    ERR("Unable to save connection data, argument is NULL !!");
    exit(EXIT_FAILURE);
  }
  
  tmp = pthread_getspecific(key);

  if (tmp != NULL){
    ERR("Unable to save connection data as an already one exist !");
    exit(EXIT_FAILURE);
  }
  
  ret=pthread_setspecific(key, pipe_info);
  
  if(ret<0)
    {
      switch(errno){
      case EINVAL:
	ERR("The private data of the worker thread is invalid",strerror(errno));
	exit(EXIT_FAILURE);
	break;
	
      case ENOMEM:
	ERR("Insufficient memory exists to associate connection data with the worker thread",strerror(errno));
	exit(EXIT_FAILURE);
	break;
      }
    }
  return EXIT_SUCCESS;
}

extern void* rt_get_private_pipe_data()
{
  return internal_get_private_pipe_data(pipe_data);
}

/**
 * Get the spÈcific connection structure associated with the current thread
 * 
 * @return a void pointer to cast in GW_PIPE_LIST_ELT_PTR or <code>NULL</code> if an error occured.
 */
static void* internal_get_private_pipe_data(pthread_key_t key)
{
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  } 
  return pthread_getspecific(key);  
}


/**
 * Destroy the connection structure associated with the current thread
 * Note: It is better to call this function MANUALLY before the thread death ;-)
 * Anyway, if it is not, unfortunatelly MANUALLY done, the runtime call it 
 * 
 * @param data, a pointer to the specific data associated with the current thread
 */
extern void rt_delete_private_pipe_data(void *data)
{
  internal_delete_private_pipe_data(data,pipe_data);
}

/**
 * Destroy the connection structure associated with the current thread
 * Note: It is better to call this function MANUALLY before the thread death ;-)
 * Anyway, if it is not, unfortunatelly MANUALLY done, the runtime call it 
 * 
 * @param data, a pointer to the specific data associated with the current thread
 */

static void internal_delete_private_pipe_data(void *data,pthread_key_t key)
{
  //  INFO("internal_delete_private_pipe_data");
  //update 26/04/08 
  //GW_CONN_PTR conn=(GW_CONN_PTR) data
  //gw_conn_free(conn);
  //When a connection is freed it implies to close it
  //so do not close it manually
  // gw_conn_close(conn); 

  //update 30/10/08
  //server_garbage_conn_elt((GW_PIPE_LIST_ELT_PTR) data);

  //update 21/11/08
  //Do nothing now as it should be done by mtl !!

  pthread_setspecific(key,NULL);
}


/* ---------------------------------------------------------------------- */
/* functions for accessing the hash table */

extern void lock_hashtable(pthread_mutex_t *htlock) {
  _lock(htlock);
}

extern void unlock_hashtable(pthread_mutex_t *htlock) {
  _unlock(htlock);
}

static LIST_ELT_PTR
hashtable_find(HASHTABLE_PTR ht,struct protocol *p, message_t msg, int conn_id) {
  struct list_element* l;
  char *s = p->get_hash(msg,conn_id);
  l=p->search(msg, conn_id, rt_hashtable_search(ht,s));
  free(s);
  return l;
}

//////////////////////////////////////////////////////////////
// 11/03/09 suppose that it is called ONLY directly from mtl
//////////////////////////////////////////////////////////////

/* called from a context where the hashtable is not locked */
/* always the left side */
extern struct list_element *
unlocked_hashtable_find(struct protocol *p, message_t msg) {
 
  int conn_id=-1; 
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  struct list_element *l;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  if(p->type==TCP)
    {
      //Get thread specific data
      if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
	{
	  ERR("Failed to get specific connection data for this thread !!");
	  exit(EXIT_FAILURE);
	}
      conn_id=pipe_elt->conn_in->sock;
    }
	
  lock_hashtable(&left_hashtable_lock); // lock before reading
  l = hashtable_find(left_hashtable,p,msg,conn_id);
  unlock_hashtable(&left_hashtable_lock);
  return l;
}

/* ---------------------------------------------------------------------- */

/**
 * Save data to the runtime hastable shared among all existing threads
 * 
 * @param hash, hash code associated with the list_element to insert
 * @param l, a new list element to save
 */
static void
rt_save_public_conn_data(HASHTABLE_PTR ht,pthread_mutex_t *htlock,
			 struct list_element *l)
{
	
  LIST_ELT_PTR elt=NULL;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }

  //Lock the hashtable before reading
  lock_hashtable(htlock);
  if(l==NULL){
    ERR("Attempt to save a NULL data into the runtime hashtable");
    exit(EXIT_FAILURE);
  }

  //Get the list head from the hashtable if exists
  elt=rt_hashtable_search(ht, l->hash);
	
  //Do we Need to create a new entry in the runtime hashtable ?
  if(elt==NULL)
    { 
      //Add a new head list in the hashtable
      //elt=(LIST_ELT_PTR)malloc(sizeof(struct list_element));
      elt=(LIST_ELT_PTR)malloc(sizeof(LIST_ELT));
      
      elt->key=NULL;
      elt->hash=NULL;
      elt->env=NULL;
      elt->genv=NULL;
      elt->cont=NULL;
      
      //assert(elt);
      INIT_LIST_HEAD(&elt->_list);
      list_add(&l->_list,&elt->_list);
      //INIT_LIST_HEAD(&(l->_list));
      //      char* hash=copy_string(l->hash);
      rt_hashtable_insert(ht,l->hash,elt);
      //rt_hashtable_insert(ht,l->hash,l);
      //elt=rt_hashtable_search(ht, l->hash);
      //INFO("**** Search hash : %s not found", l->hash);
      //INFO("**** Search hash : %s found", elt->hash);
    }
  else // collision in the hashtable 
    {
      WARN("Collision in the runtime hastable detected !!");
      list_add(&l->_list,&elt->_list);
    }

  //Unlock the hashtable
  unlock_hashtable(htlock);
}

/* ---------------------------METHODS MANIPULATED BY MTL------------------------------------ */

/**
 *
 *
 */
extern GW_CONN_LIST_ELT_PTR 
rt_get_conn_from_id(int id_proto, int conn_id, struct list_head* conn_elt_list){
  GW_CONN_LIST_ELT_PTR elt=NULL;
  INFO("Searching proto_id:%d <=> sock_id: %d",id_proto,conn_id);  

  list_for_each_entry(elt, conn_elt_list,list)
    {
      if(elt->id==id_proto){
	if((conn_id>0)&&(elt->conn!=NULL)){
	  INFO("Found: proto_id: %d and sock_id: %d",elt->id,elt->conn->sock);
	  if(elt->conn->sock==conn_id){
	    return elt;
	  }
	}else if (conn_id<0){
	  INFO("Found: proto_id: %d and sock_id: %d and conn==NULL", elt->id,conn_id);
	  return elt;
	}
      }
    }
  INFO("Found Nothing"); 
  return NULL;
}

extern GW_CONN_LIST_ELT_PTR 
rt_del_conn_from_id(int id_proto, int conn_id, struct list_head* conn_elt_list){
  struct list_head *pos, *q;
  GW_CONN_LIST_ELT_PTR elt=NULL;
  INFO("Searching to remove proto_id:%d <=> sock_id: %d",id_proto,conn_id);  
  
  list_for_each_safe(pos,q, conn_elt_list){
    elt = list_entry(pos, GW_CONN_LIST_ELT, list); 
    if(elt->id==id_proto){
      if((conn_id>0)&&(elt->conn!=NULL)){
	INFO("Remove: proto_id: %d and sock_id: %d",elt->id,elt->conn->sock);
	if(elt->conn->sock==conn_id){
	  list_del(pos);
	  return elt;
	}
      }else if (conn_id<0){
	INFO("Remove: proto_id: %d and sock_id: %d and conn==NULL", elt->id,conn_id);
	list_del(pos);
	return elt;
      }
    }
  }
  INFO("Found Nothing");  
  return NULL;
}

/**
 * Body of the send
 */
static GW_CONN_LIST_ELT_PTR 
send_sync(struct protocol *p,char *m, size_t len) {
  
  //  INFO("send_sync !");
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  GW_CONN_LIST_ELT_PTR conn_elt;
  
  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
    {
      ERR("Failed to get specific connection data for this thread !!");
      exit(EXIT_FAILURE);
    }
  
  assert(pipe_elt->conn_ready);
  conn_elt=pipe_elt->conn_ready;
  
  if(conn_elt->conn==NULL){
    ERR("Runtime panic: send_sync called whereas no destination host has been set up");
    exit(-1);
  }
  
  if(gw_conn_send_without_buffering(conn_elt->conn,m,len)!=EXIT_SUCCESS){
    ERR("unable to send data");
    exit(-1);	
  }
  
  ERR("Sent:\n%s",m);
  free(m);
  
  return conn_elt;
}


/**
 * SEND SYNC 
 *
 * Method called by mtl. This function performs the sending of message
 *
 */
extern void* resp_send_sync(struct protocol *p,char *m, size_t len) {
  ERR("resp_send_sync");
  char* data;
  GW_CONN_LIST_ELT_PTR conn_elt =(GW_CONN_LIST_ELT_PTR)send_sync(p,m, len);

  assert(conn_elt!=NULL);
  (conn_elt->conn)->recv_hdlr((void*)(conn_elt->conn));

  //get the buffer
  data=gw_conn_get_received_msg(conn_elt->conn);
  /*
  if(!p->alive){
    INFO("Not Dancing alive");
    conn_elt->is_alive=CONN_NOT_ALIVE;
  }else{
    conn_elt->is_alive=CONN_KEEP_ALIVE;
    } */   
  //server_set_delete_flag(conn_elt);
  
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // IMPOSSIBLE TO FREE HERE THE CONNECTION AS ITS BUFFER WILL BE USED BY THE PARSER 
  // SO DATA IS FREED LATER WHEN internal_delete_private_pipe_data is called
  // internal_delete_private_pipe_data each time mtl is called 
  WARN("resp_send_sync");
  return data;
}

/**
 * SEND SYNC 
 *
 * Method called by mtl. This function performs the sending of message
 *
 */
extern void void_send_sync(struct protocol *p,char *m, size_t len) {
  ERR("void send sync starting");
  GW_CONN_LIST_ELT_PTR conn_elt;

  conn_elt=(GW_CONN_LIST_ELT_PTR)send_sync(p,m, len);

  assert(conn_elt!=NULL);
  /*
  if(!p->alive){
    INFO("Not Dancing alive");
    //    server_set_delete_flag(conn_elt);
    conn_elt->is_alive=CONN_NOT_ALIVE;
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // IMPOSSIBLE TO FREE HERE THE CONNECTION AS ITS BUFFER WILL BE USED BY THE PARSER 
    // SO DATA IS FREED LATER WHEN internal_delete_private_pipe_data is called
    // internal_delete_private_pipe_data each time mtl is called 
  }else{
    conn_elt->is_alive=CONN_KEEP_ALIVE;
    }*/
  WARN("void send sync ending");
}

/**
 * Assume that before a tcp_connect, a set_host has been done
 */
static GW_CONN_LIST_ELT_PTR tcp_connect(struct protocol *p) {
  INFO("tcp_or_udp_connect !");
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;
  GW_CONN_LIST_ELT_PTR conn_elt=NULL;
  
  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
    {
      ERR("Failed to get specific pipe data for this thread !!");
      exit(EXIT_FAILURE);
    }
  
   _lock(&pipe_elt->_lock);

  //Search a connection that has been previously setted througth set_host
  //p->conn_id should be here equal to -1
  if((conn_elt=rt_get_conn_from_id(p->proto_id,-1, &pipe_elt->conn_out_list))==NULL)
    {
      ERR("Runtime panic: try to get a connection that does not exist");
      exit(-1);
    }
  
  if(conn_elt->is_open!=CONN_CLOSE)
    {
      ERR("Runtime panic: try to create a connection that already exists and alive");
      exit(-1);     
    }
 
  switch(conn_elt->socktype){
  case SOCK_STREAM :
    //Create a new tcp connection
    conn_elt->conn=gw_create_new_client_tcp_connection(conn_elt->host,conn_elt->port);
    conn_elt->conn->recv_hdlr=conn_elt->receive_handler;
    conn_elt->is_open=CONN_OPEN;
    break;
  case SOCK_DGRAM : 
    //Create a new udp connection
    WARN("udp_connect in fact");

    conn_elt->conn=gw_create_new_client_udp_connection(conn_elt->host,conn_elt->port);//bugs?

    conn_elt->conn->recv_hdlr=conn_elt->receive_handler;
    conn_elt->is_open=CONN_OPEN;
    break;
  }

  //Set multicast flag if required
  if(conn_elt->is_multicast)
    sock_set_multicast(conn_elt->conn->sock,conn_elt->host);

  //Update the current connection id in the proto structure
  //p->conn_id=conn_elt->conn->sock;
  
  //15/03/09 ok update the conn ready
  pipe_elt->conn_ready=conn_elt;
  
  //  ERR("conn_elt sock: %d",conn_elt->conn->sock);

  if(!p->alive){
    INFO("Not Dancing alive");
    conn_elt->is_alive=CONN_NOT_ALIVE;
  }else{
    conn_elt->is_alive=CONN_KEEP_ALIVE;
  }
  _unlock(&pipe_elt->_lock);
  WARN("tcp_or_udp_connect !");
  return conn_elt;
}
   
/**
 * Initiate a tcp connect without waiting a response
 *
 * Method called by mtl. 
 *
 */
extern void void_tcp_connect(struct protocol *p) {
  tcp_connect(p);
}

/**
 * Initiate a tcp connect and for a response 
 *
 */
extern void *resp_tcp_connect(struct protocol *p) {
  
  GW_CONN_LIST_ELT_PTR conn_elt;
  char * data;
  conn_elt=tcp_connect(p);
  assert(conn_elt!=NULL);
  (conn_elt->conn)->recv_hdlr((void*)(conn_elt->conn));
  data=gw_conn_get_received_msg(conn_elt->conn);
  return data;
}

/**
 * Get the current connection id on the right side of the gateway
 *
 */
extern int tcp_get_connection(struct protocol *p)
{
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  // GW_CONN_LIST_ELT_PTR conn_elt;
  
  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
    ERR("Failed to get specific connection data for this thread !!");
    exit(EXIT_FAILURE);
  }
  
  assert(pipe_elt->conn_ready->is_open!=CONN_CLOSE);
  
  //return conn_elt->conn->sock;
  return pipe_elt->conn_ready->conn->sock;//ADDED 04/04/09
}

/**
 *
 * Return a boolean indicating if the connection has been found
 */
extern int tcp_use_connection(struct protocol *p,int sock_id){

  ERR("tcp_user_connection starting");

  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }

  GW_PIPE_LIST_ELT_PTR pipe_elt;
  GW_CONN_LIST_ELT_PTR conn_elt;
	
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
    ERR("Failed to get specific connection data for this thread !!");
    exit(EXIT_FAILURE);
  }
  
  if((conn_elt=rt_get_conn_from_id(p->proto_id, sock_id,&pipe_elt->conn_out_list))==NULL){
    server_lock(pipe_elt->gw);
    conn_elt=rt_get_conn_from_id(p->proto_id, sock_id,&pipe_elt->gw->conn_out_list_alive);
    server_unlock(pipe_elt->gw);
  }
  assert(conn_elt!=NULL);
  //assert(pipe_elt->conn_ready==NULL);
  //If conn_elt == NULL The connection has not been saved yet. 
  
  pipe_elt->conn_ready=conn_elt;
  
  /*
  if(p->alive){
    INFO("Searching in the server memory structure an alive connection: sock %d", sock_id);
    conn_elt=rt_get_conn_from_id(p->proto_id, sock_id,&pipe_elt->gw->conn_out_list_alive);

  }else{ //As it was before 
    conn_elt=rt_get_conn_from_id(p->proto_id, sock_id,&pipe_elt->conn_out_list);
    pipe_elt->conn_ready=conn_elt; //ADDED 04/04/09
    }*/
  
  WARN("tcp_user_connection ending");
  /* 
 if(conn_elt==NULL)
    return 0;
    else*/
    return 1;
}

/**
 *
 *
 */
extern void tcp_close(struct protocol *p)
{
  ERR("tcp_close! starting");
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  GW_CONN_LIST_ELT_PTR conn_elt;
  
  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
    ERR("Failed to get specific connection data for this thread !!");
    exit(EXIT_FAILURE);
  }
  
  //04/04/09L
  //  if((conn_elt=rt_del_conn_from_id(p->proto_id, p->conn_id,&pipe_elt->conn_out_list))==NULL)
  // conn_elt=rt_del_conn_from_id(p->proto_id, p->conn_id,&pipe_elt->gw->conn_out_list_alive);
  
  if((conn_elt=rt_del_conn_from_id(p->proto_id, pipe_elt->conn_ready->conn->sock,&pipe_elt->conn_out_list))==NULL){
    server_lock(pipe_elt->gw);
    conn_elt=rt_del_conn_from_id(p->proto_id, pipe_elt->conn_ready->conn->sock,&pipe_elt->gw->conn_out_list_alive);
    server_unlock(pipe_elt->gw);
  }
  //  conn_elt=pipe_elt->conn_ready;
  
  if(conn_elt->conn==NULL){
    ERR("Runtime panic: tcp_close called whereas no connection has been set up");
    exit(-1);
  }
  //assert(conn_elt!=NULL);
  //assert(conn_elt->conn!=NULL);
  
  // if(conn_elt->is_open==CONN_CLOSE){
  //  WARN("Runtime warning: try to close a connection already closed !!");
  //  return;
  //}
  
  //gw_conn_close(conn_elt->conn);
  //conn_elt->is_open=CONN_CLOSE;
  
  //Issue here with tcp-alive.
  // conn_elt is never freed
  // conn_elt get from pipe_elt->conn_out_list and never freed.
  //conn_elt->is_open=CONN_CLOSE;
  
  if(conn_elt->host!=NULL)
    free(conn_elt->host);
  conn_elt->host=NULL;
  gw_conn_free(conn_elt->conn);
  free(conn_elt);
  conn_elt=NULL;
  pipe_elt->conn_ready=NULL;
  
  //rt_auto_clean_up_conn_from_id(p->proto_id, p->conn_id,pipe_elt);
  ERR("tcp_close! stoping");
}


/**
 *
 *
 */
extern GW_PIPE_LIST_ELT_PTR set_host(int id,const char* ip, int port, int type, int multicast,  void (*handler)(void *), void* (*receive)(void *))
{
  //  INFO("set_host");
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  GW_CONN_LIST_ELT_PTR conn_elt;

  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
      ERR("Failed to get specific connection data for this thread !!");
      exit(EXIT_FAILURE);
    }

  //Instanciate a new conn_elt structure but do not initiate any connection
  conn_elt=pipe_add_conn_out(pipe_elt,id, ip,port, type, multicast, handler, receive);
  assert(conn_elt!=NULL);
  //INFO("ADD proto id:%d host:%s:%d",conn_elt->id,conn_elt->host,conn_elt->port);
  return pipe_elt;
}

void set_host_tcp (int id,char* ip, int port, void* (*tcp_receive)(void *)) {
  (void)set_host (id, ip, port, SOCK_STREAM, 0, NULL, tcp_receive);
}

void set_host_udp (int id,char* ip, int port, int multicast, void (*handler)(void *)) {
  INFO("set_host_udp id:%d ip:%s port:%d flag:%d",id,ip, port, multicast);
  //GW_PIPE_LIST_ELT_PTR set_host (id, ip, port, SOCK_DGRAM, multicast, handler, NULL);
  (void)set_host (id, ip, port, SOCK_DGRAM, multicast, handler, NULL);
  //
  
}

/**
 *
 *
 */

/* ---------------------------------------------------------------------- */

extern void restart(struct protocol *right_protocol, void* msg_view)
{
  ERR("restart call");
  void* genv=NULL;
  void* env=NULL;
  void (*cont)(void *env,void *genv, message_t resp);
  int conn_id=-1;

  LIST_ELT_PTR l=NULL;
  LIST_ELT_PTR v=NULL;
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }

  INFO("count: %d",hashtable_count(right_hashtable));

  //Lock the hashtable before reading
  lock_hashtable(&right_hashtable_lock);
    
  char *s=NULL;
  s = right_protocol->get_hash(msg_view,conn_id);

  v=rt_hashtable_search(right_hashtable,s);

  assert(v!=NULL);
  
  free(s);  
  s=NULL;
  l=right_protocol->search(msg_view, conn_id, v);

  /*
  if((l = hashtable_find(right_hashtable,right_protocol,msg_view, conn_id))==NULL)
    {    
      ERR("Failed to restart mtl !!");
      //Unlock the hashtable
      unlock_hashtable(&right_hashtable_lock);
      //return;
      exit(EXIT_FAILURE);
    }
  */

  //Get the current data
  assert(l->env!=NULL);
  env=l->env;
  genv=l->genv;
  cont=l->cont;
	
  assert(cont!=NULL);

  //Free the list_element

  list_del(&l->_list);  

  //if no collision remove from hashtable
  if(list_empty(&v->_list)) {
    rt_hashtable_remove(right_hashtable,l->hash);
    free(v);
    v=NULL;
  }

  //list_del(&(l->_list));
  //if(list_empty(&(l->_list))) //if no collision remove from hashtable
  //  rt_hashtable_remove(right_hashtable,l->hash);
  
  //Unlock the hashtable
  unlock_hashtable(&right_hashtable_lock);
  
  //10/04/09
  //if(l->hash)
  //  free(l->hash); has been freed with rt_hashtable_remove
  
  right_protocol->free_key(l->key);
  free(l);
  
  //20/04/09
  //get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
    ERR("Failed to get specific connection data for this thread !!");
    exit(EXIT_FAILURE);
  }
  
  _lock(&pipe_elt->_lock);
  server_rm_fd_set(pipe_elt->gw, pipe_elt->conn_ready->conn->sock);
  //  pipe_remove_from_dispatcher(pipe_elt); This is done in pipe_ready
  _unlock(&pipe_elt->_lock);

  ERR("call continuation");
  cont(genv,env,msg_view);
  ERR("restart ending");
  //18/05/2010
  //gw_conn_close(pipe_elt->conn_ready->conn);
  //gw_conn_free(pipe_elt->conn_ready->conn);
    free_pipe_0(pipe_elt);
}

extern void 
void_send_async(struct protocol *p,char *m, size_t len){
  ERR("void send async starting");
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }

  (void)tcp_connect(p);  
  (void)send_sync(p,m, len);
  WARN("void send async ending");
}

/**
 *
 *
 */
extern void resp_send_async(struct protocol *p,
			    template msg, 
			    char *m, 
			    size_t len, 
			    void *genv,
			    void *env,
			    void (*cont)(void *env,void *genv, message_t)) {
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  INFO("resp_send_async call");
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;
  GW_CONN_LIST_ELT_PTR conn_elt=NULL;
  
  LIST_ELT_PTR l=NULL;

  //22/04/09
  //=======================================================/
  //Connect the socket
  
  conn_elt=tcp_connect(p);

  //=======================================================/
  //Local operations specific for udp
  
  //get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL){
    ERR("Failed to get specific connection data for this thread !!");
    exit(EXIT_FAILURE);
  }
  
  //13/04/09
  _lock(&pipe_elt->_lock);
  
  //=======================================================//
  //Search a connection that has been previously setted througth set_host
  //  if((conn_elt=rt_get_conn_from_id(p->proto_id,-1, &pipe_elt->conn_out_list))==NULL){
  //  ERR("Runtime panic: try to get a connection that does not exist");
  //  exit(-1);
  //}
  
  //15/03/09 ok update the conn ready
  //  pipe_elt->conn_ready=conn_elt;
  
  //if(!p->alive){
  //  INFO("Not Dancing alive");
  //  conn_elt->is_alive=CONN_NOT_ALIVE;
  //}else{
  //  conn_elt->is_alive=CONN_KEEP_ALIVE;
  //} 
  //Removed 22/04/09
  
  //19/04/09
  pipe_add_to_dispatcher(pipe_elt);  
  

  INFO("connection used id:%d , sock:%d",conn_elt->id,conn_elt->conn->sock);

  
  //=======================================================//
  l=(LIST_ELT_PTR)malloc(sizeof(struct list_element));
  
  assert(p!=NULL);
  
  if(p==NULL){
    ERR("Runtime panic protocol structure is empty");
    exit(EXIT_FAILURE);
  }
  
  assert(p->create_key!=NULL);
  l->key=p->create_key((message_t)msg,pipe_elt->conn_ready->conn->sock);//04/04/09
  
  //assert(p->get_hash!=NULL);
  assert(msg!=NULL);
  l->hash=copy_string(p->pre_hash(msg));
  
  assert(env);
  l->env=env;
  assert(l->env);
  l->genv=genv;
  l->cont=cont;
  
  //Save data relative to this message
  //  INFO("<<<<<<<<<<<<<<<<<<<< %s",l->hash);
  rt_save_public_conn_data(right_hashtable,&right_hashtable_lock,l);
  
  //Arm the continuation
  server_add_fd_set(pipe_elt->gw,conn_elt->conn->sock);

  //Send data to protocol B
  gw_conn_send_without_buffering(conn_elt->conn,m,len);
  
  free(m);
  //  ERR("Sent:\n%s",m);
  //13/04/09
  _unlock(&pipe_elt->_lock);
  
  WARN("resp_send_async ended");
}

/**
 *
 *
 */
extern void session_start(struct protocol *p, message_t msg, void *genv)
{
  ERR("session_start starting");
  LIST_ELT_PTR l=NULL;
  int conn_id=-1; 
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  if(p->type==TCP)
    {
      //Get thread specific data
      if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
	{
	  ERR("Failed to get specific connection data for this thread !!");
	  exit(EXIT_FAILURE);
	}
      conn_id=pipe_elt->conn_in->sock;
    }

  lock_hashtable(&left_hashtable_lock); // lock before reading
  l = hashtable_find(left_hashtable,p,msg,conn_id);
  unlock_hashtable(&left_hashtable_lock);

  if(l != NULL)
    {    
      ERR("Session already exists !!");
      exit(EXIT_FAILURE);
    }
  
  l=(LIST_ELT_PTR)malloc(sizeof(LIST_ELT));
  
  l->key=p->create_key(msg,conn_id);
  // l->hash=copy_string(p->get_hash(msg,conn_id));
  l->hash=p->get_hash(msg,conn_id);
  l->genv=genv;
  rt_save_public_conn_data(left_hashtable,&left_hashtable_lock,l);
  WARN("session_start ending");
}

/**
 *
 *
 */
extern void session_end(struct protocol *p, message_t msg)
{
  ERR("session_end starting");
  GW_PIPE_LIST_ELT_PTR pipe_elt=NULL;  
  LIST_ELT_PTR v=NULL; //Vector
  LIST_ELT_PTR l=NULL; //elt inside the vector
  int conn_id=-1;

  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  //Lock the hashtable before reading
  lock_hashtable(&left_hashtable_lock);
  
  if(p->type==TCP)
    {
      //Get thread specific data
      if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
	{
	  ERR("Failed to get specific connection data for this thread !!");
	  exit(EXIT_FAILURE);
	}
      conn_id=pipe_elt->conn_in->sock;
    }

  char *s=NULL;
  s = p->get_hash(msg,conn_id);
  v=rt_hashtable_search(left_hashtable,s);
  free(s);  
  s=NULL;
  l=p->search(msg, conn_id, v);
  list_del(&(l->_list));  

  //if no collision remove from hashtable
  if(list_empty(&v->_list)) {
    rt_hashtable_remove(left_hashtable,l->hash);
    free(v);
    v=NULL;
  }
  
  if(l->key)
    p->free_key(l->key);

  free(l);
  
  /*
  if((l1 = hashtable_find(left_hashtable,p,msg,conn_id))==NULL)
    {    
      ERR("Can't find session !!");
      exit(EXIT_FAILURE);
    }
  
  */


  //Remove l from the list
  //list_del(&l1->_list);
  //l2=rt_hashtable_remove(left_hashtable,l1->hash);  
  
  
  
  /*
  */
  //Bugs
  
  //Free the list_element
  //  list_del(&(l->_list));
  //if(list_empty(&(l->_list))) //if no collision remove from hashtable
  //
  
  //Unlock the hashtable
  unlock_hashtable(&left_hashtable_lock);
  
  //15/04/09
  //if(l->hash)
  //  free(l->hash);
  
  //INFO("After removal, hashtable contains %u items.\n",
  //hashtable_count(left_hashtable));

  ERR("session_end ending");
}

static void check_final_return(struct list_head* conn_elt_list)
{/*
  struct list_head *pos, *q;
  GW_CONN_LIST_ELT_PTR elt=NULL;
  INFO("Searching to remove proto_id:%d <=> sock_id: %d",id_proto,conn_id);  
  
  list_for_each_safe(pos,q, conn_elt_list){
    elt = list_entry(pos, GW_CONN_LIST_ELT, list); 
    // Do not de - allocate a connection that should be alive
    if(elt->id==id_proto){
      if((conn_id>0)&&(elt->conn!=NULL)){
	INFO("Remove: proto_id: %d and sock_id: %d",elt->id,elt->conn->sock);
	if(elt->conn->sock==conn_id){
	  list_del(pos);
	  return elt;
	}
      }else if (conn_id<0){
	INFO("Remove: proto_id: %d and sock_id: %d and conn==NULL", elt->id,conn_id);
	list_del(pos);
	return elt;
      }
    }
  }
  INFO("Found Nothing");  */
}

/**
 *
 *
 */
extern void final_return(struct protocol *left_protocol,char *m, size_t len)
{	
  ERR("final_return starting");
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }
  
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
    {
      ERR("Failed to get specific connection data for this thread !!");
      exit(EXIT_FAILURE);
    }
  
  _lock(&pipe_elt->_lock); //22/04/09

  if (pipe_elt->conn_ready==NULL)
    {
      ERR("------------- tcp_close has been called ");
      goto end;
    }else if(pipe_elt->conn_ready->is_alive==CONN_SAVED){
    goto end;
  }
  
  if(left_protocol->alive)
    goto end;
  
  if ((pipe_elt->conn_ready->is_alive==CONN_KEEP_ALIVE)){
    ERR("---> sock: %d alive (1): %d open (1): %d",pipe_elt->conn_ready->conn->sock,pipe_elt->conn_ready->is_alive,pipe_elt->conn_ready->is_open);      
    WARN("SAVE");
    rt_del_conn_from_id(pipe_elt->conn_ready->id,pipe_elt->conn_ready->conn->sock, &pipe_elt->conn_out_list);
    server_lock(pipe_elt->gw);
    server_save_conn_alive(pipe_elt->gw, pipe_elt->conn_ready);
    server_unlock(pipe_elt->gw);
    pipe_elt->conn_ready=NULL;
  }
  
 end:
  INFO("sock_id: %d", pipe_elt->conn_in->sock);
  gw_conn_send_without_buffering(pipe_elt->conn_in,m,len);
  free(m);
  
  //17/05/2010
  if(gw_conn_get_sock_type(pipe_elt->conn_in)==SOCK_DGRAM){
    server_rm_fd_set(pipe_elt->gw, pipe_elt->conn_in->sock);
    //gw_conn_close(pipe_elt->conn_in);
    //gw_conn_free(pipe_elt->conn_in); 
    // the free is done at the end of restart
  }
  _unlock(&pipe_elt->_lock);  

  WARN("final_return ending");
}

/**
 *
 *
 */
extern void preturn(char *m, size_t len) {

  ERR("preturn starting");
  
  GW_PIPE_LIST_ELT_PTR pipe_elt;
	
  if(isInitialized!=1){
    ERR("Runtime subsystem is not initialized !!");
    exit(EXIT_FAILURE);
  }

  //Get thread specific data
  if((pipe_elt=(GW_PIPE_LIST_ELT_PTR)rt_get_private_pipe_data())==NULL)
    {
      ERR("Failed to get specific connection data for this thread !!");
      exit(EXIT_FAILURE);
    }
  
  gw_conn_send_without_buffering(pipe_elt->conn_in,m,len);
  free(m);
  WARN("preturn ending");
}

/**
 *
 */
extern void rt_finish()
{
  ERR("rt_finish called : TO BE DONE");
}
