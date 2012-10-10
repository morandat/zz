#include "z2z_lock.h"
#include "z2z_log.h"

extern int _lock(pthread_mutex_t *lock){
  int ret;    
  if ((ret = pthread_mutex_lock(lock)) != 0) {
    ERR("Failed to lock the server thread structure");
    switch (ret) {
    case EINVAL:
      WARN("The value specified by mutex is invalid");
      break;
    case EDEADLK:
      WARN("A deadlock would occur if the thread blocked waiting for mutex");
      break;
    }
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}

extern int _unlock(pthread_mutex_t *lock){
  int ret;    
  if ((ret = pthread_mutex_unlock(lock)) != 0) {
    ERR("Failed to unlock the server thread structure");
    switch (ret) {
    case EINVAL:
      WARN("The value specified by mutex is invalid");
      break;
    case EDEADLK:
      WARN("A deadlock would occur if the thread blocked waiting for mutex");
      break;
    }
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}

extern int _destroy_lock(pthread_mutex_t *lock){
  int ret;    
  if ((ret = pthread_mutex_destroy(lock)) != 0) {
    ERR("Failed to lock the server thread structure");
    switch (ret) {
    case EINVAL:
      WARN("The value specified by mutex is invalid");
      break;
    case EDEADLK:
      WARN("A deadlock would occur if the thread blocked waiting for mutex");
      break;
    case EPERM:
      WARN("The current thread does not hold a lock on mutex");
      break;
    case EBUSY: WARN("EBUSY"); 
      break;
    }
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


extern Z2Z_LOCK_PTR gw_create_tcp_lock(){
  
  Z2Z_LOCK_PTR lock=NULL;
  
  if ((lock =(Z2Z_LOCK_PTR)malloc(sizeof(Z2Z_LOCK))) == NULL) {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    return (NULL);
  }

  if(pthread_mutex_init(&(lock->done_mutex),NULL))
    {
      free(lock);
      ERR("Failed to create a receive server thread mutex !");
      ERR("errno: %s",strerror(errno));
      return NULL;
    }
  if(pthread_cond_init(&(lock->is_active),NULL))
  {
    _destroy_lock(&(lock->done_mutex));
    free(lock);
    ERR("Failed to create a receive server thread condition mutex");
    ERR("errno: %s",strerror(errno));
    return NULL;
  }
  if(pthread_cond_init(&(lock->not_active),NULL))
  {
    _destroy_lock(&(lock->done_mutex));
    pthread_cond_destroy(&(lock->is_active));
    free(lock);
    ERR("Failed to create a receive server thread condition mutex");
    ERR("errno: %s",strerror(errno));
    return NULL;
  }

  lock->active=0;
  return lock;
}

int gw_destroy_tcp_lock(Z2Z_LOCK_PTR lock) {
  int ret;
  if (pthread_cond_destroy(&(lock->is_active))){
    ERR("Failed to DESTROY thread condition is_active");
    ERR("errno: %s",strerror(errno));
    exit (EXIT_FAILURE);
  }
  
  if (pthread_cond_destroy(&(lock->not_active))){
    ERR("Failed to DESTROY thread condition not_active");
    ERR("errno: %s",strerror(errno));
    exit (EXIT_FAILURE);
  }

  (void)_destroy_lock(&lock->done_mutex);

  free(lock);
  lock=NULL;
  return EXIT_SUCCESS;
}

Z2Z_MTL_LOCK_PTR create_lock(){
  Z2Z_MTL_LOCK_PTR lock=NULL;
  if ((lock =(Z2Z_MTL_LOCK_PTR)malloc(sizeof(Z2Z_MTL_LOCK))) == NULL) {
    WARN("Failed to allocate connection: %s\n", strerror(errno));
    return (NULL);
  }
  
  if(pthread_mutex_init(&(lock->mutex),NULL)){
    free(lock);
    ERR("Failed to create a receive server thread mutex !");
    ERR("errno: %s",strerror(errno));
    return NULL;
  }
  return lock;
}

void lock(Z2Z_MTL_LOCK_PTR lock){
  (void)_lock(&lock->mutex);
}

void unlock(Z2Z_MTL_LOCK_PTR lock){
  (void)_unlock(&lock->mutex);
}

void destroy_lock(Z2Z_MTL_LOCK_PTR lock){
  (void)_destroy_lock(&lock->mutex);
  free(lock);
  lock=NULL;
}


