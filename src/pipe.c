#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <z2z_socket.h> // from z2z
#include <z2z_connection.h> // GW_CONN_PTR
#include <z2z_log.h>
#include <z2z_dlist.h>
#include <z2z_server.h>     // GW_SERV_PARAM_PTR
#include <z2z_pipe.h>


/**
 *
 */
extern GW_PIPE_LIST_ELT_PTR 
pipe_new(GW_SERV_PARAM_PTR srv_param, GW_CONN_PTR conn_in, GW_CONN_LIST_ELT_PTR conn_out)
{
  //  INFO("server_new_pipe");
  int ret=0;
  GW_PIPE_LIST_ELT_PTR pipe_elt;
  if ((pipe_elt = (GW_PIPE_LIST_ELT_PTR) malloc(sizeof(GW_PIPE_LIST_ELT))) == NULL) {
    ERR("Failed to allocate server thread structure");
    return NULL;
  }
  pipe_elt->conn_ready = NULL;
  pipe_elt->gw = srv_param;
  if (conn_in == NULL)
    WARN("!! PIPE ALOCATION WITH A INCOMING CONNECTION SET TO NULL: MAY FAILED ");
  pipe_elt->conn_in = conn_in;
  INIT_LIST_HEAD(&pipe_elt->conn_out_list);
  INIT_LIST_HEAD(&pipe_elt->list);
  if(conn_out!=NULL)
    list_add(&(conn_out->list),&(pipe_elt->conn_out_list));
  //  INFO("server_new_pipe_ended");
  if ((ret=pthread_mutex_init(&pipe_elt->_lock, NULL)) != 0) {
    ERR("Failed to initialize the lock of conn_elt");
  }
  return pipe_elt;
}

/**
*
*/
extern GW_CONN_LIST_ELT_PTR pipe_add_conn_out(GW_PIPE_LIST_ELT_PTR pipe_elt, int id,const char* host, int port, int type, int multicast,void (*request_handler)(void * arg), void* (*receive_handler)(void *))
{
  GW_CONN_LIST_ELT_PTR elt=NULL;
  char* h=NULL;
  size_t s=0;
  //bugs?
  s=strlen(host)+1;
  if(s>0){
    h=malloc(sizeof(char)*s);
    assert(h);
    strcpy(h,host);
  }
  
  elt=server_init_new_conn_elt(id,h,port,type,multicast);
  if(receive_handler!=NULL) 
    elt->receive_handler=receive_handler;
  if(request_handler!=NULL) 
    elt->request_handler=request_handler;

  _lock(&pipe_elt->gw->gw_conn_out_list_alive_lock); //Added 13/04/09    
  list_add(&(elt->list),&(pipe_elt->conn_out_list));
  _unlock(&pipe_elt->gw->gw_conn_out_list_alive_lock); //Added 13/04/09    

  return elt;
}

/**
 * The main thread listen on a set of sockets that are added dynamically by mtl for doing continuations
 */
extern int 
pipe_add_to_dispatcher(GW_PIPE_LIST_ELT_PTR pipe_elt)
{
  ERR("Add pipe to dispatcher starting");
  _lock(&pipe_elt->gw->gw_pipe_list_lock);
  list_add(&pipe_elt->list, &pipe_elt->gw->gw_pipe_list);
  _unlock(&pipe_elt->gw->gw_pipe_list_lock);
  
  WARN("Add pipe to dispatcher ending");
  return 0;
}

/**
 * 
 */
extern int 
pipe_remove_from_dispatcher(GW_PIPE_LIST_ELT_PTR pipe_elt)
{
  //Already done in pipe_ready
  ERR("Remove pipe to dispatcher starting");
  _lock(&pipe_elt->gw->gw_pipe_list_lock);
  if (pipe_elt->conn_ready == NULL){
    WARN("tentative to remove this conn whereas no data is received yet");
    return 0;
  }
  
  INFO(">>>>>>> Remove fd %d", pipe_elt->conn_ready->conn->sock);

  list_del(&pipe_elt->list);
  pipe_elt->list.next=NULL;

  _unlock(&pipe_elt->gw->gw_pipe_list_lock);
  WARN("Remove pipe to dispatcher ending");
  return 0;
}
