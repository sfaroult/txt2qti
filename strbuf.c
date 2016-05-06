/// \file  strbuf.c
/// \brief Management of strings without any length limit.
/* -------------------------------------------------------------*
 
   Utility functions for handling strings of arbitrary length
 
   Written by Stephane Faroult

 * -------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "strbuf.h"

#define CHUNK    256

extern void strbuf_init(STRBUF *sb) {
   if (sb) {
      sb->len = 0;
      sb->curlen = 0;
      sb->s = (char *)NULL;
   }
}

extern void strbuf_dispose(STRBUF *sb) {
   if (sb && sb->len) {
      free(sb->s);
      sb->s = (char *)NULL;
      sb->len = 0;
      sb->curlen = 0;
   }
}

extern void strbuf_clear(STRBUF *sb) {
   // Clear without freeing memory
   if (sb && sb->len) {
      sb->s[0] = '\0';
      sb->curlen = 0;
   }
}

extern void strbuf_unquote(STRBUF *sb) {
   int i;

   if (sb && sb->len && (sb->curlen > 1)) {
      if (((sb->s[0] == '\'')
           || (sb->s[0] == '"')) 
          && (sb->s[sb->curlen - 1] == sb->s[0])) {
         // Shift everything
         for (i = 1; i < sb->curlen - 1; i++) {
           sb->s[i - 1] = sb->s[i];
         }
         sb->curlen -= 2;
         sb->s[sb->curlen] = '\0';
      }
   }
}

extern void strbuf_trim(STRBUF *sb) {
   int i;
   int start;

   if (sb && sb->len && sb->curlen) {
      while (sb->curlen
             && isspace(sb->s[sb->curlen - 1])) {
        (sb->curlen)--;
      }
      sb->s[sb->curlen] = '\0';
      i = 0;
      while (isspace(sb->s[i])) {
        i++;
      }
      start = i;
      if (start) {
         // Shift everything
         for (i = start; i < sb->curlen; i++) {
           sb->s[i - start] = sb->s[i];
         }
         sb->curlen -= start;
         sb->s[sb->curlen] = '\0';
      }
   }
}

extern void strbuf_add(STRBUF *sb,
                       char   *s) {
   size_t required;

   if (sb && s) {
      required = sb->curlen + strlen(s) + 1;
      required = ((required % CHUNK) == 0 ? required
                   : (CHUNK * (1 + (int)required/CHUNK)));
      if (0 == sb->len) {
         if ((sb->s = (char *)malloc(required)) == (char *)NULL) {
            perror("malloc()");
            exit(1);
         }
         sb->len = required;
         strcpy(sb->s, s);
      } else {
         if (required > sb->len) {
            if ((sb->s = (char *)realloc(sb->s, required)) == (char *)NULL) {
               perror("realloc()");
               exit(1);
            }
            sb->len = required;
         }
         strcat(sb->s, s);
      }
      sb->curlen = strlen(sb->s);
   }
}

extern void strbuf_nadd(STRBUF *sb,
                        char   *s,
                        size_t  len) {
   size_t required;

   if (sb && s) {
      required = sb->curlen + len + 1;
      required = ((required % CHUNK) == 0 ? required
                   : (CHUNK * (1 + (int)required/CHUNK)));
      if (0 == sb->len) {
         if ((sb->s = (char *)malloc(required)) == (char *)NULL) {
            perror("malloc()");
            exit(1);
         }
         sb->len = required;
         strncpy(sb->s, s, len);
         sb->s[len] = '\0';
      } else {
         if (required > sb->len) {
            if ((sb->s = (char *)realloc(sb->s, required)) == (char *)NULL) {
               perror("realloc()");
               exit(1);
            }
            sb->len = required;
         }
         strncat(sb->s, s, sb->curlen + len);
         sb->s[sb->curlen + len] = '\0';
      }
      sb->curlen = strlen(sb->s);
   }
}

extern void strbuf_addc(STRBUF *sb, int c) {
  size_t required;

  if (sb) {
    if (sb->curlen >= (sb->len - 1)) {
      required = sb->len + CHUNK;
      if (0 == sb->len) {
        if ((sb->s = (char *)malloc(required)) == (char *)NULL) {
          perror("malloc()");
          exit(1);
        }
      } else {
        if ((sb->s = (char *)realloc(sb->s, required)) == (char *)NULL) {
          perror("realloc()");
          exit(1);
        }
      }
      sb->len = required;
    }
    sb->s[sb->curlen] = (char)c;
    (sb->curlen)++;
    sb->s[sb->curlen] = '\0';
  }
}

extern void strbuf_concat(STRBUF *sb1,
                          STRBUF *sb2) {
   if (sb1 && sb2) {
      strbuf_add(sb1, sb2->s);
   }
}

