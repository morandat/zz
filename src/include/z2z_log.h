#ifndef Z2Z_LOG_H
#define Z2Z_LOG_H
#endif

#include <syslog.h>   // for variable LOG_WARNING, LOG_ERR, etc

#include <stdio.h>    // for structure FILE

#define RESET 0
#define BRIGHT 1
#define DIM 2
#define UNDERLINE 3
#define BLINK 4
#define REVERSE 7
#define HIDDEN 8

#define BLACK 0
#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define MAGENTA 5
#define CYAN 6
#define WHITE 7

/* A Warning message */ 
#ifdef DEBUG
#define WARN(fmt,...) zlog(stderr, LOG_WARNING, __FILE__,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__);

/* An error message*/

#define ERR(fmt,...) zlog(stderr, LOG_ERR, __FILE__,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__);

/* A general information message */
#define INFO(fmt,...) zlog(stderr, LOG_INFO, __FILE__,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__);

#else

#define WARN(fmt,...) ((void)0)
#define ERR(fmt,...) ((void)0)
#define INFO(fmt,...) ((void)0)
#endif

extern void zlog(FILE* fd,int priority, const char *filename, const char *function, int line, const char *s,...);



/*
LOG_EMERG
A panic condition was reported to all processes.
LOG_ALERT
A condition that should be corrected immediately.
LOG_CRIT
A critical condition.
LOG_ERR
An error message.
LOG_WARNING
A warning message.
LOG_NOTICE
A condition requiring special handling.
LOG_INFO
A general information message.
LOG_DEBUG
A message useful for debugging programs.
*/
