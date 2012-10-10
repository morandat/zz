#ifndef _Z2Z_LOCK_H_
#define _Z2Z_LOCK_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h> //pthread_create()
#include <stdlib.h> // for EXIT_SUCCESS and EXIT_FAILURE
#include <errno.h>      // for variable errno
#include <string.h>     // for method strerror()

struct z2z_lock {
  int active;
  pthread_mutex_t done_mutex;
  pthread_cond_t is_active;
  pthread_cond_t not_active;
};

typedef struct z2z_lock Z2Z_LOCK;
typedef Z2Z_LOCK* Z2Z_LOCK_PTR;

struct z2z_mtl_lock {
  pthread_mutex_t mutex;
};

typedef struct z2z_mtl_lock Z2Z_MTL_LOCK;
typedef Z2Z_MTL_LOCK* Z2Z_MTL_LOCK_PTR;


extern Z2Z_LOCK_PTR gw_create_tcp_lock();
extern int gw_destroy_tcp_lock(Z2Z_LOCK_PTR lock);

extern Z2Z_MTL_LOCK_PTR create_lock();

extern int _lock(pthread_mutex_t *lock);
extern int _unlock(pthread_mutex_t *lock);
extern int _destroy_lock(pthread_mutex_t *lock);

extern void lock(Z2Z_MTL_LOCK_PTR lock);
extern void unlock(Z2Z_MTL_LOCK_PTR lock);
extern void destroy_lock(Z2Z_MTL_LOCK_PTR lock);

#define ATOMIC_LOOP_BEGIN(conn)				\
  Z2Z_LOCK_PTR lock=(conn)->z2z_lock;			\
  do {						\
      pthread_mutex_lock(&(lock->done_mutex));	\
       while(lock->active!=0) {			\
	 pthread_cond_wait(&(lock->not_active),&(lock->done_mutex));\
       }

#define EXIT_LOOP()				\
  goto end_loop;

#define ATOMIC_LOOP_END()			\
  (lock->active)=1;				\
  pthread_cond_signal(&(lock->is_active));	\
  pthread_mutex_unlock(&(lock->done_mutex));	\
  }while(1);					\
end_loop:					\
 (lock->active)=1;				\
 gw_conn_set_status(conn,CONN_EOF);		\
 pthread_cond_signal(&(lock->is_active));	\
 pthread_mutex_unlock(&(lock->done_mutex));	
#endif // _Z2Z_LOCK_H_
