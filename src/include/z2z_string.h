#ifndef __Z2Z_STRING_H__
#define __Z2Z_STRING_H__

struct string {
  int ref_count;
  char *val;
};

char *copy_string(char *str);
struct string *make_string(char *str);
struct string *make_string_n(char *str,int len);
struct string *make_string_nomalloc(char *str);
void dec_string_unknown(struct string *s);
void inc_string(struct string *s);
char * string_of_int (int val);
struct string *concat_string(char *str, char* str2); // to be removed when 'cons' is ready
char* addSlashes(char* buf, int bufSize);

#endif /* __Z2Z_STRING_H__ */
