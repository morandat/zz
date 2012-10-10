#ifndef __MTL_H__
#define __MTL_H__
#include "z2z_server.h"

#define DEFINE_RESPONSE_HANDLER(hname)		\
  static void response_handler(void * arg)	\
  {							\
    GW_PIPE_LIST_ELT_PTR pipe_elt;			\
    pipe_elt = (GW_PIPE_LIST_ELT_PTR)arg;		\
    if( !arg ) {							\
      ERR("%s: Received null argument pointer!");			\
      return;								\
    }									\
    rt_save_private_pipe_data(pipe_elt);				\
    hname (pipe_elt->conn_ready->conn->rbuflenused,(char*)(pipe_elt->conn_ready->conn->rbuf)); \
    rt_delete_private_pipe_data(pipe_elt);				\
  }

#define DEFINE_TCP_CLOSE_HANDLER(hname)					\
  static void tcp_close_handler (void * arg)				\
  {									\
    GW_PIPE_LIST_ELT_PTR pipe_elt;					\
    pipe_elt = (GW_PIPE_LIST_ELT_PTR)arg;				\
  if( !arg ) {								\
    ERR("%s: Received null argument pointer!");				\
    return;								\
  }									\
  rt_save_private_pipe_data(pipe_elt);					\
  hname ();								\
  rt_delete_private_pipe_data(pipe_elt);				\
  }

#define DEFINE_TCP_CONNECT_HANDLER(hname)				\
  static void tcp_connect_handler (void * arg)				\
  {									\
    GW_PIPE_LIST_ELT_PTR pipe_elt;					\
    pipe_elt = (GW_PIPE_LIST_ELT_PTR)arg;				\
    if( !arg ) {							\
      ERR("%s: Received null argument pointer!");		\
      return;								\
    }									\
    rt_save_private_pipe_data(pipe_elt);				\
    hname ();								\
    rt_delete_private_pipe_data(pipe_elt);				\
  }

#define DEFINE_REQUEST_HANDLER(hname)					\
  static void request_handler (void * arg)				\
  {									\
    GW_PIPE_LIST_ELT_PTR pipe_elt;					\
    pipe_elt = (GW_PIPE_LIST_ELT_PTR)arg;				\
    if( !arg ) {							\
      ERR("%s: Received null argument pointer!");		\
      return;								\
    }									\
    rt_save_private_pipe_data(pipe_elt);				\
    hname (pipe_elt->conn_in->rbuflenused,(char*)(pipe_elt->conn_in->rbuf)); \
    rt_delete_private_pipe_data(pipe_elt);				\
  }
#endif
