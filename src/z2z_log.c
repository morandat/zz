#include <stdio.h> /* vfprintf */
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h> /* va_start, va_end, va_copy */
#include <pthread.h>
#include "z2z_log.h"

/**
 * Global Variables
 */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Change the color of the terminal according to the log level
 */
void textcolor(FILE* fd,int attr, int fg, int bg)
{
  char command[13];
  /* Command is the control command to the terminal */
  sprintf(command, "%c[%d;%d;%dm", 0x1B, attr, fg + 30, bg + 40);
  fprintf(fd,"%s", command);
}

extern void zlog(FILE* fd,int priority, const char *filename, const char *function, int line, const char *s,...)
{

  va_list ap;
  va_start(ap, s);

#ifdef HAVE_VA_COPY
  va_list ap_copy;
#endif
  
  int rc = pthread_mutex_lock(&log_mutex);
  if(rc){ /*an error has occured */
    perror("pthread_mutex_lock");
    pthread_exit(NULL);
  }

  switch(priority)
    {
    case LOG_WARNING : textcolor(fd,RESET, WHITE, BLACK);
      break;
    case LOG_ERR     : textcolor(fd,RESET, RED, BLACK);
      break;
    case LOG_INFO    : textcolor(fd,RESET, BLUE, WHITE);
      break;
    }

  fprintf(fd,"%u %s (fonction <%s> ligne %d): ", (unsigned int)pthread_self(),filename, function, line);  

#ifdef HAVE_VA_COPY
  va_copy(ap_copy, ap);
  vfprintf(fd, s, ap_copy);
  va_end(ap_copy);
#else
  vfprintf(fd, s, ap);
#endif
  fprintf(fd,"\n");
  textcolor(fd,RESET, BLACK, WHITE);
  fflush(fd);
  rc = pthread_mutex_unlock(&log_mutex);
  if(rc){ /*an error has occured */
    perror("pthread_mutex_unlock");
    pthread_exit(NULL);
  }
  va_end(ap);
}


/*
static int open_log(char* file) {
    if((LOG= fopen(file,"a+")) == (FILE *)NULL) {
      LogError("%s: Error opening the log file '%s' for writing -- %s\n",
	       prog, Run.logfile, STRERROR);
      return(FALSE);
    }
    // Set logger in unbuffered mode
    setvbuf(LOG, NULL, _IONBF, 0);
  }
  return TRUE;
}
*/
