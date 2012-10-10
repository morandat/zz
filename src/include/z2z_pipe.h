#ifndef _Z2Z_PIPE_H_
#define _Z2Z_PIPE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <z2z_server.h>

extern GW_PIPE_LIST_ELT_PTR 
pipe_new(GW_SERV_PARAM_PTR srv_param, GW_CONN_PTR conn_in, GW_CONN_LIST_ELT_PTR conn_out);

extern GW_CONN_LIST_ELT_PTR 
pipe_add_conn_out(GW_PIPE_LIST_ELT_PTR pipe_elt, int id,const char* host, int port, int type, int multicast,void (*request_handler)(void * arg), void* (*receive_handler)(void *));

extern int 
pipe_add_to_dispatcher(GW_PIPE_LIST_ELT_PTR pipe_elt);

extern int 
pipe_remove_from_dispatcher(GW_PIPE_LIST_ELT_PTR pipe_elt);
#endif // _Z2Z_SERVER_H_
