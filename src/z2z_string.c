#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "z2z_string.h"

char *copy_string(char * val) {
  size_t len = strlen(val) + 1;
  char *s = (char *)malloc (sizeof(char) * len);
  memcpy (s, val, len);
  return s;
}

static char *copy_string_n(char * val, int len) {
  char *s = (char *)malloc (sizeof(char) * (len+1));
  memcpy (s, val, len);
  s[len] = '\0';
  return s;
}

struct string *make_string(char *str) {
  if (str == NULL) return str;
  struct string *s = malloc(sizeof(*s));
  s->ref_count = 1;
  s->val = copy_string(str);
  return s;
}

// This function was added, because the 'cons' function
// is not implemented yet...
struct string *concat_string(char *str, char* str2) {
   char* r = (char *)calloc(strlen(str) + strlen(str2) + 1, 
                        sizeof(char));

  struct string *s = malloc(sizeof(*s));
  
  strcat(r, str);
  strcat(r, str2);

  s->ref_count = 1;
  s->val = copy_string(r);
    
  free(r);
  return s;
}


struct string *make_string_n(char *str,int len) {
  struct string *s = malloc(sizeof(*s));
  s->ref_count = 1;
  s->val = copy_string_n(str,len);
  return s;
}

struct string *make_string_nomalloc(char *str) {
  struct string *s = malloc(sizeof(*s));
  s->ref_count = 1;
  s->val = str;
  return s;
}

void dec_string_unknown(struct string *s) {
  int ref_count;
  if (s == NULL) return;
  ref_count = s->ref_count;
  if (ref_count == -1) return;
  if (ref_count == 1) {
    if (s->val) free(s->val);
    free(s);
  }
  else s->ref_count = ref_count - 1;
}

void inc_string(struct string *s) {
  int ref_count = s->ref_count;
  if (ref_count == -1) return;
  s->ref_count = ref_count + 1;
}


char * 
string_of_int (int val) {
  char buf[25];
  sprintf (buf, "%d", val);
  size_t len = strlen(buf);
  void *s = (char *) malloc (sizeof (char) * (len+1)); // LR: why + 1
  memcpy (s, buf, len+1);
  return s;
}


char* addSlashes(char* buf, int bufSize){
  char* newBuf;
  char *d;
  int i;
  int cnt;

  cnt = 0;
  for(i=0;i<bufSize;i++){
    if(buf[i] == '\r' || buf[i] == '\n'){
      cnt++;
    }
  }

  newBuf = (char*)malloc(bufSize+cnt+1);

  d = newBuf;
  for(i=0;i<bufSize;i++){
    if (buf[i] == '\r'){
      *d = '\\'; d++;
      *d = 'r';
    } else if (buf[i] == '\n'){
      *d = '\\'; d++;
      *d = 'n';
    }else {
      *d = buf[i];
    }
    d++;
  }
  *d='\0';

  return newBuf;
}
