#ifndef __TEMPLATE_H__
#define __TEMPLATE_H__

#include <stdlib.h>
#include "z2z_string.h"

struct template_;
typedef struct template_ *template;

struct template_cst_ {
  int code;           //  unique idenfier for a template 
  void (*sending_req_function)(template);
  void (*sending_resp_function)(template,void *);
  void (*freeing_function)(template);
  size_t len;         //  size of the text, including the '\0' character
  int nb_places;      //  Number of placeholder in the template
  int nb_holes;       //  Number of holes in the template
  int holes[];        //  holes[i]   is the index of the place in the ith hole in the template text
                      //  holes[i+1] is the position of the ith hole in the template text
};

extern template template_new (const struct template_cst_ *cst, const char *text);
extern void template_free (template t);

extern char * template_flush (template templ, size_t *len);
extern struct string * template_flush_to_string (template templ);
extern void template_sending_req (template templ);
extern void template_sending_resp (template templ,void *);

extern void template_fill_int    (template templ,int index,int val);
extern void template_fill_string (template templ,int index,struct string*val);

extern void template_fill_int8     (template templ,int index, int val);
extern void template_fill_int16    (template templ,int index, int val);

extern int template_get_code (template templ);
extern void * template_get_index(template templ, int index);

extern void dec_template_unknown(template templ);
extern void inc_template(template templ);

#endif /* __TEMPLATE_H__ */
