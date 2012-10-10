#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_STRNSTR

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "z2z_strnstr.h"

char * strnstr (const char *haystack, const char *needle, size_t hlen)
{
   size_t nlen;
                                                                              
   if (!needle || !*needle)
       return (char *)haystack;
   else
       nlen = strlen (needle);
                                                                              
   while (hlen >= nlen) {
       if (*needle == *haystack && !strncmp (haystack + 1, needle + 1,
                                             nlen - 1))
           return (char *) haystack;
       haystack++; hlen--;
   }
                                                                              
   return NULL;
}
#endif
