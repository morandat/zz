#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "z2z_template.h"
#include "z2z_string.h"
#include "z2z_list.h"
#include "z2z_log.h"

enum ty { INT, FRAGMENT, LIST };

struct template_ {
  const struct template_cst_ * cst;  // Reference to the default values
  int ref_count;
  const char *text;  //  text of the template 
  size_t *places_len;    // places_len[i] is the length of the ith palceholder
  enum ty *places_type; // places_type[i] is the type of the ith palceholder
  void **places_val;  // places_val[i] is the value  of the ith placeholder
};

int template_get_code (template templ) {
  return templ->cst->code;
}

template template_new (const struct template_cst_ *cst, const char *text) {
  //INFO("template_new call");
  int i;
  template templ = (template) malloc (sizeof(*templ));
  templ->cst = cst;
  templ->text = text;
  int places = cst->nb_places;

  // optimization; a template is always created to be stored in a variable
  templ->ref_count = 1;
  
  templ->places_len = (size_t *) malloc (sizeof(size_t) * places);

//INFO("test");
  templ->places_type = (enum ty *) malloc (sizeof(enum ty) * places);
  templ->places_val = (void **) malloc (sizeof(void *) * places);
  
  for (i = 0; i < places; i++) {
    templ->places_len[i] = 0;
    templ->places_val[i] = NULL;
  }
  return templ;
}


void template_free (template t) {
  assert(t);
  free(t->places_len);
  free(t->places_val);
  free(t->places_type);
  free(t);
}


// for send and return
extern char * template_flush (template templ, size_t *_len) {
  //INFO ("template_flush: start");
  assert (templ);
  int i;
  const struct template_cst_ *cst = templ->cst;
  int nb_holes  = cst->nb_holes;
  size_t len = cst->len;  
  for (i = 0; i < nb_holes; i++) {
    len += templ->places_len[cst->holes[i<<1]];
  }
  char *buf  = (char *)malloc(sizeof(char) * (len + 1)); // '\0'  char
  int curr = 0, pos = 0;
  for (i = 0; i < nb_holes; i++) {
    int index = cst->holes[i<<1];
    if (templ->places_val[index]) {
      int place = cst->holes[1+(i<<1)];
      if (place != curr) {
	size_t len = place - curr;
	memcpy(buf+pos, templ->text+curr, len);
	pos += len;
	curr += len;
      }
      size_t len = templ->places_len[index];
      switch (templ->places_type[index]) {
      case INT:
	memcpy(buf+pos, (char *)(templ->places_val[index]), len);
	break;
      case FRAGMENT:
	memcpy(buf+pos, ((struct string *)(templ->places_val[index]))->val,
	       len);
	break;
      case LIST:
	printf("not handling lists\n");
	exit(1);
      }
      pos += len;
    }
  }
  if (curr < cst->len/*was len*/) {
    memcpy(buf+pos, templ->text+curr, cst->len/*was len*/-curr);
  }
  //printf("curr %d len %d pos %d\n",curr,len,pos);
  buf[len]='\0';
  if (_len) *_len = len;

  // INFO ("template_flush: end. value=\n<%s>\n\n", buf);
  return buf;
}

// for internal use by mtl
extern struct string * template_flush_to_string (template templ) {
  struct string *s = malloc(sizeof(*s));
  size_t _i;
  s->ref_count = 0;
  s->val = template_flush(templ,&_i);
  return s;
}

void template_sending_req(template templ) {
  if (templ->cst->sending_req_function)
  templ->cst->sending_req_function(templ);
}

void template_sending_resp(template templ,void *req) {
  //INFO("bug");
  //assert(templ->cst->sending_resp_function!=NULL);
  if (templ->cst->sending_resp_function)
    templ->cst->sending_resp_function(templ,req);
}

static void 
template_fill (template templ, int index, enum ty t, void * val, size_t len) {
  templ->places_len[index] = len;
  templ->places_type[index] = t;
  templ->places_val[index] = val;
}

void 
template_fill_int (template templ, int index, int val) {
  char *s = string_of_int (val);
  template_fill (templ, index, INT, s, strlen(s));
}

void 
template_fill_int8 (template templ, int index, int val) {
  char *buf = (char *) malloc(sizeof(char));
  *buf = (int8_t)val;
  template_fill (templ, index, INT, buf, 1);
}

void 
template_fill_int16 (template templ, int index, int val) {
  char *buf = (char *) malloc(sizeof(char) * 2);
  int16_t v = (int16_t)val;
  buf[0] = (v & 0xff00) >> 8;
  buf[1] =  v & 0x00ff ; 
  template_fill (templ, index, INT, buf, 2);
}


void 
template_fill_string (template templ, int index, struct string * val) {
  template_fill (templ, index, FRAGMENT, (void *)val,
		 strlen(val->val));
}

void 
template_fill_list (template templ, int index, list val) {
//  template_fill (templ, index, LIST, (void *)val);
  printf("lists not supported\n"); exit(1);
}

void * template_get_index(template templ, int index) {
  return templ->places_val[index];
}

/* ----------------------------------------------------------------------- */
// reference count management

void dec_template_unknown(template templ) {
  int r;
  if (templ == NULL) return;
  // there are no static templates, ie with ref count -1
  r = templ->ref_count;
  if (r == 1) templ->cst->freeing_function(templ);
  else templ->ref_count = r - 1;
}

void inc_template(template templ) {
  templ->ref_count = templ->ref_count + 1;
}
