/* The interface between the run-time system and the psl/mtl */
#ifndef __RUNTIME_H__
#define __RUNTIME_H__

#include "z2z_template.h"
#include "z2z_dlist.h"
#include "z2z_hashtable.h"
#include "z2z_server.h"

typedef void * hash_key_t;
typedef void * message_t;

typedef int hash_value_t;

typedef struct hashtable* HASHTABLE_PTR;

struct list_element {
  hash_key_t key;
  char* hash;
  void* env;
  void* genv;
  void (*cont)(void *env,void *genv, message_t);
  struct list_head _list;
};

typedef  struct list_element          LIST_ELT;
typedef  LIST_ELT*          LIST_ELT_PTR;

extern int rt_init();
extern void rt_delete_private_pipe_data(void *data);
extern int rt_save_private_pipe_data(void* pipe_info);
extern void* rt_get_private_pipe_data();

enum proto_t {
  TCP =0,
  UDP=1,
};
typedef enum proto_t PROTO;

struct protocol {
  // idenfies a specific protocol, must be unique
  int proto_id;

  //->08/04/09 remove conn_id
  // 20/11/08 may have several conn per protocol id now
  //int conn_id; // conn_id == the current socket used 20/11/08 
  //-> Not possible anymore to maintain a a current socket as mtl is now multithreaded

  // Used by send_sync in order to know if the connection needs to be closed
  int alive; // 0 or 1
  
  PROTO type; //20/11/08 not sure this is usefull now
  
  // Invoked after receiving a message.  This function invokes the parser
  // to create the view and then either calls the mtl for a request or
  // calls restart for a response
  void (*dispatch)(int len, char *buf);
  // Invoked by send 
  hash_key_t (*create_key)(message_t t, int conn_id);
  // Invoked by send_async, to get the hash code from the template to send 
  char* (*pre_hash)(message_t t);
  // Invoked by restart 
  char* (*get_hash)(message_t m, int conn_id); //11/03/09 add a conn_id 
  // Invoked by restart 
  struct list_element *(*search)(message_t m, int conn_id, struct list_element *l);
 // Invoked by return 
  void (*free_key)(hash_key_t key);
};

typedef struct protocol protocol_info;

extern void void_send_sync(struct protocol *p, char *m, size_t len);
extern void *resp_send_sync(struct protocol *p, char *m, size_t len);

extern void void_send_async(struct protocol *p,char *m, size_t len);
extern void resp_send_async(struct protocol *p,
		       template msg, char *m, size_t len, void *genv,
		       void *env,
		       void (*cont)(void *env,void *genv, message_t));

extern int tcp_get_connection(struct protocol *p);
extern int tcp_use_connection(struct protocol *p,int id_conn);

extern void void_tcp_connect(struct protocol *p);
extern void *resp_tcp_connect(struct protocol *p);
extern void final_return(struct protocol *left_protocol,char *m, size_t len);

extern void preturn(char *m, size_t len);
extern void restart(struct protocol *p, message_t msg);

extern void tcp_close(struct protocol *p);
extern void set_host_tcp (int id,char* ip, int port, void* (*tcp_receive)(void *));
extern void set_host_udp (int id,char* ip, int port, int multicast, void (*handler)(void *));

//TO DO to put in lib/utils
//need to modify the protocol_rtsp.c generated file
hash_value_t hash_string(char* arg);
void session_start(struct protocol *p, message_t msg, void *genv);
void session_end(struct protocol *p, message_t msg);
void check_right_hashtable();
extern struct list_element * unlocked_hashtable_find(struct protocol *p, message_t msg);
#endif /* __INTERFACE_H__ */
