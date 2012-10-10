#ifndef __LIST_H__
#define __LIST_H__

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct elt_ *list;

int    null(list l);
void * head(list l);
list   tail(list l);
list   cons(void *x, list  xs);

void dec_list_unknown(list l, void (*dec_elt)(void *));
void inc_list(list l);
#endif /* __LIST_H__ */
