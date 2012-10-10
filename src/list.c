#include "include/z2z_list.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>



struct elt_ {
  int ref_count;
  void *val;
  list next;
};


int
null(list l) {
  return (l == NULL);
}

void *
head(list l) {
  assert(l);
  return (l->val); 
}

list
tail(list l) {
  return (l->next);
}

list
cons(void *x, list  xs) {
  list elt = (list)malloc(sizeof(*elt));
  elt->ref_count = 0;
  elt->val = x;
  elt->next = xs;
  if (xs) xs->ref_count = xs->ref_count + 1;
  return elt;
}

/* ----------------------------------------------------------------------- */
// reference count management

void dec_list_unknown(list l, void (*dec_elt)(void *)) {
  int r;
  if (l == NULL) return;
  // there are no static lists, ie with ref count -1
  r = l->ref_count;
  if (r == 1) {
    if (dec_elt) dec_elt(l->val);
    dec_list_unknown(l->next,dec_elt);
    free(l);
  }
  else l->ref_count = r - 1;
}

void inc_list(list l) {
  if (l != NULL) l->ref_count = l->ref_count + 1;
}
 
