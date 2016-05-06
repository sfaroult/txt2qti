/*
 *  Turn text-file quizzes into a QTI zip file.
 *
 *  Written by Stéphane Faroult
 *
 *  Uses miniz (https://code.google.com/archive/p/miniz/) by Rich Geldreich
 *  and a md5 implementation found on the web.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#include "strbuf.h"
#include "miniz.h"
#include "md5.h"

#define OPTIONS         "?hamvdt:"

#define START_CODE      "<pre>"
#define END_CODE        "</pre>"

#define LINE_LEN        4000
#define BUFFER_LEN       250
#define CHOICE_ID_LEN     10
#define CHOICE_ALLOC       5
#define IDENT_LEN         50
#define ZIP_SIZE       10240
#define ZIP_ALLOC       5120
#define MAX_ROMAN         20

// Question types
#define  QTYPE_UNKNOWN     0

// Respondus only knows 1 to 5
#define  QTYPE_MULTCHOICE  1    // Multiple Choice
#define  QTYPE_TF          2    // True/False
#define  QTYPE_ESSAY       3    
#define  QTYPE_MULTANSW    4    // Multiple Answer
#define  QTYPE_FILL        5    // Fill-in the Blank

//
// States
#define STATE_NONE      0
#define STATE_QUESTION  1
#define STATE_CHOICE    2
#define STATE_ANSWER    3

// Formats
#define FMT_NONE       -1
#define FMT_UNKNOWN     0
#define FMT_NUMERICAL   1
#define FMT_ULETTER     2
#define FMT_LLETTER     3
#define FMT_UROMAN      4
#define FMT_LROMAN      5

typedef struct choice {
           char *id;
           char  correct;
           char *text;
           char *feedback;
          } CHOICE_T;

typedef struct num_fmt {
           char  style;
           char  sep;
          } NUM_FMT_T; 

// Global flags
static char         G_mixed_format = 0;
static char         G_no_answers = 0;
static char         G_verbose = 0;
static char         G_debug = 0;

// Formats (numbering)
static NUM_FMT_T    G_qformat = {FMT_UNKNOWN, '.'}; // Question format
static NUM_FMT_T    G_cformat = {FMT_UNKNOWN, '.'}; // Choice format

// Other global variables
static char        G_first_choice[CHOICE_ID_LEN] = "";
static const char *G_roman[MAX_ROMAN] =
                  {"I", "II", "III", "IV", "V",
                   "V", "VII", "VIII", "IX", "X",
                   "XI", "XII", "XIII", "XIV", "XV",
                   "XVI", "XVII", "XVIII", "XIX", "XX"};
static const char *G_statename[] =
                  {"Undefined state",
                   "Question",
                   "Choice",
                   "ANSWER",
                   NULL};

static void trimstr(char *p) {
    int len;

    if (p) {
       len = strlen(p);
       while (len && isspace(p[len-1])) {
         len--;
       }
       p[len] = '\0';
    }
}

static void trimstr2(char *p) {
    int len;

    if (p) {
       len = strlen(p);
       while (len && (isspace(p[len-1]) || ispunct(p[len-1]))) {
         len--;
       }
       p[len] = '\0';
    }
}

static void html_safe_stradd(STRBUF *sp, char *s) {
    char *p;

    if (G_debug) {
      fprintf(stderr, "> html_safe_stradd\n");
    }
    if (sp && s) {
      p = s;
      while (*p) {
        switch(*p) {
          case '<' :
               strbuf_add(sp, "&lt;");
               break;
          case '>' :
               strbuf_add(sp, "&gt;");
               break;
          case '"' :
               strbuf_add(sp, "&quot;");
               break;
          case '#' :
               strbuf_add(sp, "&#35;");
               break;
          case '&' :
               strbuf_add(sp, "&amp;");
               break;
          default:
               strbuf_addc(sp, *p);
               break;
        }
        p++;
      }
    }
    if (G_debug) {
      fprintf(stderr, "< html_safe_stradd\n");
    }
}

static char *next_fmt(int num, char *next, NUM_FMT_T format) {
    // num is the current number
    if (next) {
      if ((format.style == FMT_NONE)
          || (format.style == FMT_UNKNOWN)) {
        return NULL;
      }
      switch(format.style) {
        case FMT_NUMERICAL:
             sprintf(next, "%d%c", num + 1, format.sep);
             break;
        case FMT_ULETTER:
             sprintf(next, "%c%c", 'A' + num, format.sep);
             break;
        case FMT_LLETTER:
             sprintf(next, "%c%c", 'a' + num, format.sep);
             break;
        case FMT_UROMAN:
             if (num < MAX_ROMAN) {
               sprintf(next, "%s%c", G_roman[num], format.sep);
             }
             break;
        case FMT_LROMAN:
             if (num < MAX_ROMAN) {
               char  lowroman[10];
               char *p = lowroman;
               strncpy(lowroman, G_roman[num], 10);
               while (*p) {
                 *p = tolower(*p);
                 p++;
               }
               sprintf(next, "%s%c", lowroman, format.sep);
             }
             break;
        default:
             return NULL;
      }
    }
    return next;
}

static char *manifest_qti_1_2(char *manifestid,
                              char *identifier,
                              char *title) {
  STRBUF     b;

  if (G_debug) {
    fprintf(stderr, "> manifest_qti_1_2\n");
  }
  strbuf_init(&b);
  if (identifier) {
    strbuf_add(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_add(&b, "<manifest identifier=\"");
    strbuf_add(&b, manifestid);
    strbuf_add(&b, "\"\n  xmlns=\"http://www.imsglobal.org/xsd/imscp_v1p1\"\n");
    strbuf_add(&b, "  xmlns:imsmd=\"http://www.imsglobal.org/xsd/imsmd_v1p2\"\n");
    strbuf_add(&b, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    strbuf_add(&b, "  xsi:schemaLocation=\"http://www.imsglobal.org/xsd/imscp_v1p1.xsd http://www.imsglobal.org/xsd/imsmd_v1p2p2.xsd\">\n");
    strbuf_add(&b, "	<metadata>\n");
    strbuf_add(&b, "		<schema>IMS Content</schema>\n");
    strbuf_add(&b, "		<schemaversion>1.1.3</schemaversion>\n");
    strbuf_add(&b, "		<imsmd:lom>\n");
    strbuf_add(&b, "			<imsmd:general>\n");
    strbuf_add(&b, "				<imsmd:title>\n");
    strbuf_add(&b, "					<imsmd:langstring xml:lang=\"en-US\">");    if (title) {
      strbuf_add(&b, title);
    } else {
      strbuf_add(&b, "TXT2QTI Quiz Import");
    }
    strbuf_add(&b, "</imsmd:langstring>\n");
    strbuf_add(&b, "				</imsmd:title>\n");
    strbuf_add(&b, "			</imsmd:general>\n");
    strbuf_add(&b, "		</imsmd:lom>\n");
    strbuf_add(&b, "	</metadata>\n");
    strbuf_add(&b, "	<organizations />\n");
    strbuf_add(&b, "	<resources>\n");
    strbuf_add(&b, "		<resource identifier=\"RESOURCE1\" type=\"imsqti_xmlv1p1\" href=\"");
    strbuf_add(&b, identifier);
    strbuf_add(&b, ".xml\">\n");
    strbuf_add(&b, "			<file href=\"");
    strbuf_add(&b, identifier);
    strbuf_add(&b, ".xml\"/>\n");
    strbuf_add(&b, "		</resource>\n");
    strbuf_add(&b, "	</resources>\n");
    strbuf_add(&b, "</manifest>\n");
  }
  if (G_debug) {
    fprintf(stderr, "< manifest_qti_1_2\n");
  }
  return b.s;
}

static char * qti_1_2_header(char *identifier, char *title) {
  STRBUF s;

  if (G_debug) {
    fprintf(stderr, "> qti_1_2_header\n");
  }
  strbuf_init(&s);
  if (identifier) {
    strbuf_add(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_add(&s, "<questestinterop\n");
    strbuf_add(&s, " xmlns=\"http://www.imsglobal.org/xsd/ims_qtiasiv1p2\"\n");
    strbuf_add(&s, " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    strbuf_add(&s, " xsi:schemaLocation=\"http://www.imsglobal.org/xsd/ims_qtiasiv1p2 ");
    strbuf_add(&s, "http://www.imsglobal.org/xsd/ims_qtiasiv1p2p1.xsd\">\n");
    strbuf_add(&s, " <assessment ident=\"a");
    strbuf_add(&s, &(identifier[1]));
    strbuf_add(&s, "\" title=\"");
    strbuf_add(&s, (title ? title : "TXT2QTI Quiz import"));
    strbuf_add(&s, "\">\n");
    strbuf_add(&s, "  <section ident=\"root_section\">\n");
  }
  if (G_debug) {
    fprintf(stderr, "< qti_1_2_header\n");
  }
  return s.s;
}

static char * qti_1_2_footer() {
  STRBUF s;

  if (G_debug) {
    fprintf(stderr, "> qti_1_2_footer\n");
  }
  strbuf_init(&s);
  strbuf_add(&s, "  </section>\n");
  strbuf_add(&s, " </assessment>\n");
  strbuf_add(&s, "</questestinterop>\n");
  if (G_debug) {
    fprintf(stderr, "< qti_1_2_footer\n");
  }
  return s.s;
}

static char * qti_1_2(int       qnum,
                      short     qtype,
                      char     *qtext,
                      CHOICE_T *qchoices,
                      short     qchoicecnt,
                      char     *ident) {
  int    i;
  STRBUF s;
  char   buffer[BUFFER_LEN];
  char   numstr[BUFFER_LEN];

  if (G_debug) {
    fprintf(stderr, "> qti_1_2\n");
    fprintf(stderr, "  choices: %hd\n", qchoicecnt);
  }
  strbuf_init(&s);
  //  qtype is either QTYPE_MULTCHOICE (one correct answer)
  //  or QTYPE_MULTANSW (n correct answers)
  strbuf_add(&s, "<item title=\"Question ");
  sprintf(numstr, "%d", qnum);
  strbuf_add(&s, numstr);
  strbuf_add(&s, "\" ident=\"txt2qti_");
  strbuf_add(&s, ident);
  strbuf_add(&s, "_q");
  strbuf_add(&s, numstr);
  strbuf_add(&s, "\">\n");
  strbuf_add(&s, " <presentation>\n");
  strbuf_add(&s, "  <material>\n");
  strbuf_add(&s, "   <mattext texttype=\"text/html\">\n");
  strbuf_add(&s, "    ");
  html_safe_stradd(&s, qtext);
  strbuf_add(&s, "   </mattext>\n");
  strbuf_add(&s, "  </material>\n");
  strbuf_add(&s, "  <response_lid ident=\"rq");
  strbuf_add(&s, numstr);
  strbuf_add(&s, "\"");
  if (QTYPE_MULTCHOICE == qtype) {
    strbuf_add(&s, " rcardinality=\"Single\">\n");
  } else {
    strbuf_add(&s, " rcardinality=\"Multiple\">\n");
  }
  strbuf_add(&s, "   <render_choice shuffle=\"No\">\n");
  for (i = 0; i < qchoicecnt; i++) {
    if (qchoices[i].id) {
      strbuf_add(&s, "    <response_label ident=\"q");
      strbuf_add(&s, numstr); 
      strbuf_add(&s, "_");
      strbuf_add(&s, qchoices[i].id); 
      strbuf_add(&s, "\">\n");
      strbuf_add(&s, "     <material><mattext texttype=\"text/plain\">");
      html_safe_stradd(&s, qchoices[i].text);
      strbuf_add(&s, "</mattext></material>\n");
      strbuf_add(&s, "    </response_label>\n");
    }
  }
  strbuf_add(&s, "   </render_choice>\n");
  strbuf_add(&s, "  </response_lid>\n");
  strbuf_add(&s, " </presentation>\n");
  strbuf_add(&s, " <resprocessing>\n");
  strbuf_add(&s, "  <outcomes>\n");
  strbuf_add(&s, "   <decvar maxvalue=\"100\" minvalue=\"0\" ");
  strbuf_add(&s, "varname=\"SCORE\" vartype=\"Integer\" defaultval=\"0\"/>\n");
  strbuf_add(&s, "  </outcomes>\n");
  strbuf_add(&s, "  <respcondition continue=\"No\">\n");
  strbuf_add(&s, "   <conditionvar>\n");
  if (QTYPE_MULTCHOICE == qtype) {
    i = 0;
    while ((i < qchoicecnt)
           && qchoices[i].id
           && !qchoices[i].correct) {
      i++;
    } 
    if ((i < qchoicecnt) && qchoices[i].id) {
      strbuf_add(&s, "    <varequal respident=\"r");
      strbuf_add(&s, numstr);
      strbuf_add(&s, "\">q");
      strbuf_add(&s, numstr);
      strbuf_add(&s, "_");
      strbuf_add(&s, qchoices[i].id);
      strbuf_add(&s, "</varequal>\n");
    }
  } else {
    // Several possible answers
    strbuf_add(&s, "    <and>\n");
    i = 0;
    while ((i < qchoicecnt)
           && qchoices[i].id) {
      if (!qchoices[i].correct) {
        strbuf_add(&s, "     <not>\n");
        strbuf_add(&s, "      <varequal respident=\"r");
        strbuf_add(&s, numstr);
        strbuf_add(&s, "_");
        strbuf_add(&s, qchoices[i].id);
        strbuf_add(&s, "\">q");
        strbuf_add(&s, numstr);
        strbuf_add(&s, "_");
        strbuf_add(&s, qchoices[i].id);
        strbuf_add(&s, "</varequal>\n");
        strbuf_add(&s, "     </not>\n");
      } else {
        strbuf_add(&s, "     <varequal respident=\"r");
        strbuf_add(&s, numstr);
        strbuf_add(&s, "_");
        strbuf_add(&s, qchoices[i].id);
        strbuf_add(&s, "\">q");
        strbuf_add(&s, numstr);
        strbuf_add(&s, "_");
        strbuf_add(&s, qchoices[i].id);
        strbuf_add(&s, "</varequal>\n");
      }
      i++;
    } 
    strbuf_add(&s, "    </and>\n");
  }
  strbuf_add(&s, "   </conditionvar>\n");
  strbuf_add(&s, "   <setvar action=\"Set\" varname=\"SCORE\">100</setvar>\n");
  strbuf_add(&s, "  </respcondition>\n");
  strbuf_add(&s, " </resprocessing>\n");
  strbuf_add(&s, "</item>\n");
  if (G_debug) {
    fprintf(stderr, "< qti_1_2\n");
  }
  return s.s;
}

static char *process_question(int       qnum,
                              char     *qtext,
                              CHOICE_T *qchoices,
                              short     qchoicecnt,
                              char     *ident) {
   short  i;
   short  correct_answers = 0;
   short  qtype;
   char   *p;

  if (G_debug) {
    fprintf(stderr, "> process_question\n");
  }
   // Note: \n not stripped
   // printf("\nQuestion %d\n%s", qnum, (qtext ? qtext : "-- None --"));
   // Identify the type of questions by the number of correct answers
   for (i = 0; i < qchoicecnt; i++) {
     if (qchoices[i].id) {
       correct_answers += qchoices[i].correct;
     }
   }
   if (correct_answers == 1) {
     qtype = QTYPE_MULTCHOICE;
   } else {
     qtype = QTYPE_MULTANSW;
   }
   p = qti_1_2(qnum, qtype, qtext,
               qchoices, qchoicecnt, ident);
   if (G_debug) {
    fprintf(stderr, "< process_question\n");
   }
   return p;
}

static short add_choice(CHOICE_T **choices_ptr,
                        short     *choice_cnt,
                        char      *id,
                        char      *text,
                        char       correct) {
    short i = 0;
    short j;
    int   len;

    if (choices_ptr && id && text) {
      if (*choices_ptr == NULL) {
        // First one
        if ((*choices_ptr = (CHOICE_T *)malloc(sizeof(CHOICE_T)*CHOICE_ALLOC))
                     != NULL) {
          for (j = 0; j < CHOICE_ALLOC; j++) {
            (*choices_ptr)[j].id = NULL;
            (*choices_ptr)[j].correct = 0;
            (*choices_ptr)[j].text = NULL;
            (*choices_ptr)[j].feedback = NULL;
          }
          *choice_cnt = CHOICE_ALLOC;
          i = 0;
        } else {
          perror("malloc");
          exit(1);
        }
      } else {
        // Already allocated. Find room
        i = 0;
        while ((i < *choice_cnt)
               && ((*choices_ptr)[i].id != NULL)) {
          i++;
        }
        if (i == *choice_cnt) {
          // Need to create room
          if ((*choices_ptr = (CHOICE_T *)realloc(*choices_ptr,
                              sizeof(CHOICE_T)*(*choice_cnt+CHOICE_ALLOC)))
                   != NULL) {
            for (j = 0; j < CHOICE_ALLOC; j++) {
              (*choices_ptr)[i+j].id = NULL;
              (*choices_ptr)[i+j].correct = 0;
              (*choices_ptr)[i+j].text = NULL;
              (*choices_ptr)[i+j].feedback = NULL;
            }
            *choice_cnt += CHOICE_ALLOC;
          } else {
            perror("realloc");
            exit(1);
          }
        }
        // Now we are able to add something at "i"
      }
      trimstr2(id);
      len = strlen(id);
      trimstr(text);
      if (len) {
        (*choices_ptr)[i].id = strdup(id);
      }
      (*choices_ptr)[i].correct = correct;
      (*choices_ptr)[i].text = strdup(text);
      return i;
    }
    return -1;
}

static void clear_choices(CHOICE_T *choices, short choice_cnt) {
    short i;

    for (i = 0; i < choice_cnt; i++) {
      if (choices[i].id) {
        free(choices[i].id);
        choices[i].id = NULL;;
      }
      if (choices[i].text) {
        free(choices[i].text);
        choices[i].text = NULL;
      }
      if (choices[i].feedback) {
        free(choices[i].feedback);
        choices[i].feedback = NULL;
      }
    }
}

static char *encode_question(char *q) {
    // Look for code blocks which must be made
    // HTML-safe (entity replacement)
    char   *p1 = q;
    char   *p2;
    STRBUF  mod_q;

    strbuf_init(&mod_q);
    while (p1 && *p1) {
      p2 = strstr(p1, START_CODE);
      if (p2) {
        *p2 = '\0';
        strbuf_add(&mod_q, p1);
        strbuf_add(&mod_q, START_CODE);
        p1 = p2 + strlen(START_CODE);
        p2 = strstr(p1, END_CODE);
        if (p2) {
          *p2 = '\0';
          html_safe_stradd(&mod_q, p1);
          strbuf_add(&mod_q, END_CODE);
          p1 = p2 + strlen(END_CODE);
        } else {
          // End tag missing
          html_safe_stradd(&mod_q, p1);
          strbuf_add(&mod_q, END_CODE);
          p1 = NULL;
        }
      } else {
        // No (or no more) code in the question
        strbuf_add(&mod_q, p1);
        p1 = NULL;
      }
    }
    return mod_q.s;
}

static char *process_file(FILE *fp, char *fname, int *qnump,
                          mz_zip_archive *pzip, char *ident) {
   char      line[LINE_LEN];
   int       linenum = 0;
   char     *p;
   char     *s;
   char     *s2;
   char     *q;
   char     *eq;
   int       len;
   short     state = STATE_NONE;
   char      after_empty_line = 1;
   char      in_code = 0;
   char      in_block = 0;
   char      answer_known = 0;
   int       qnum = *qnump;
   STRBUF    question;
   STRBUF    code;
   STRBUF    choice;
   CHOICE_T *choices = NULL;
   char      correct = 0;
   char      maybe_correct = 0;
   int       choice_num;
   char      roman = 0;
   short     choice_cnt = 0;
   char      curr_choice[CHOICE_ID_LEN]; // Current choice id
   char      next_choice[CHOICE_ID_LEN]; // Store the next choice id
   char      next_question[CHOICE_ID_LEN]; // Store the next question id
   STRBUF    xml;

   strbuf_init(&xml);
   if (fp) {
     strbuf_init(&question);
     strbuf_init(&code);
     strbuf_init(&choice);
     while (fgets(line, LINE_LEN, fp)) {
       linenum++;
       p = line;
       len = strlen(p);
       while (isspace(*p)) {
         p++;
       }
       len = strlen(p);
       if (in_code || in_block) {
         // Don't trim anything
         p = line;
       }
       if (G_debug) {
         fprintf(stderr, "\n** %s ** %3d ** ", G_statename[state], len);
       }
       if (len) {
         if (G_debug) {
           fprintf(stderr, "%s", line);
         }
         s = p;
         s2 = s;
         // First look for code block
         while ((s = strcasestr(s2, END_CODE)) != NULL) {
           if (!in_code) {
              fprintf(stderr, "*** WARNING *** %s - line %d ***"
                              " %s found while not in a block\n",
                              fname, linenum, END_CODE);
           }
           in_code = 0;
           s += 7;
           s2 = s;
         }
         if (strcasestr(s2, START_CODE)) {
           if (in_code) {
              fprintf(stderr, "*** WARNING *** %s - line %d ***"
                              " %s found while still in a block\n",
                              fname, linenum, START_CODE);
           }
           in_code = 1;
         }
         after_empty_line = 0;
         // Now analyze the line
         switch (state) {
           case STATE_NONE:
             if (G_qformat.style == FMT_UNKNOWN) {
               // Any numbering of questions?
               // Space not understood as separator for
               // questions (reason: question starting with
               // "A blahblah ...").
               switch (*p) {
                  case 'A':
                  case 'a':
                  case '1':
                  case 'i':
                  case 'I':
                       s = p + 1;
                       switch (*s) {
                         case '.':
                         case ')':
                         case '-':
                             switch (*p) {
                               case 'A':
                                    G_qformat.style = FMT_ULETTER;
                                    break;
                               case 'a':
                                    G_qformat.style = FMT_LLETTER;
                                    break;
                               case '1':
                                    G_qformat.style = FMT_NUMERICAL;
                                    break;
                               case 'i':
                                    G_qformat.style = FMT_LROMAN;
                                    break;
                               case 'I':
                                    G_qformat.style = FMT_UROMAN;
                                    break;
                               default:
                                    break;
                              }
                             G_qformat.sep = *s;
                             p = s + 1;
                             break;
                         default:
                             G_qformat.style = FMT_NONE;
                             break;
                        }
                        break;
                   default:
                        G_qformat.style = FMT_NONE;
                        break;
               }
             } else if (G_qformat.style != FMT_NONE) {
               if ((s2 = next_fmt(qnum + 1,
                                  next_question,
                                  G_qformat)) == NULL) {
                 fprintf(stderr, "Question format problem\n");
                 return NULL;
               }
             } else {
               // No numbering of questions
               G_qformat.style = FMT_NONE;
             }
             state = STATE_QUESTION;
             if (strncasecmp(p, "<block>", 7) == 0) {
               if (in_block) {
                 fprintf(stderr, "*** WARNING *** %s - line %d ***"
                                 " <block> found while still in a block\n",
                                 fname, linenum);
               }
               in_block = 1;
               p += 7;
             }
             qnum++;
             strbuf_add(&question, p);
             answer_known = 0;
             correct = 0;
             maybe_correct = 0;
             break;
           case STATE_QUESTION:
             // Are we still in a question or not?
             // Look for:
             //    a (any case) or 1 or i (any case)
             //    followed by . or ) or - or space
             //    and possibly preceded by * (meaning
             //    a correct answer in the respondus
             //    format)
             //
             s = p;
             maybe_correct = 0;
             if (*s == '*') {
               maybe_correct = 1;
               s++;
             }
             if (!G_mixed_format
                 && strlen(G_first_choice)
                 && !strncmp(s, G_first_choice, strlen(G_first_choice))) {
               state = STATE_CHOICE;
               strcpy(curr_choice, G_first_choice);
               choice_num = 1;
               correct = 0;
               in_code = 0;
               in_block = 0;
               // Must copy the next choice. Depends on format.
               if ((s2 = next_fmt(choice_num,
                                  next_choice,
                                  G_cformat)) == NULL) {
                 fprintf(stderr, "Format problem\n");
               }
             } else {
               // Either the format is mixed (we are not "remembering"
               // choice formats) or we don't know yet what the choice
               // format looks like (first question) or we don't match
               // what we expect for the first format.
               if (G_mixed_format || !strlen(G_first_choice)) {
                 // Check whether this could be a first choice
                 switch (*s) {
                   case 'A':
                   case 'a':
                   case '1':
                   case 'i':
                   case 'I':
                        s2 = s + 1;
                        if ((*s2 == '.')
                           || (*s2 == ')')
                           || (*s2 == '-')
                           || (isspace(*s2)
                               && (*s != 'I')
                               && (*s != 'a')
                               && (*s != 'A'))) {
                          // Looks good for a first choice
                          choice_num = 1;
                          correct = 0;
                          in_code = 0;
                          in_block = 0;
                          strbuf_clear(&choice);
                          switch (*s) {
                            case 'A':
                                 G_cformat.style = FMT_ULETTER;
                                 break;
                            case 'a':
                                 G_cformat.style = FMT_LLETTER;
                                 break;
                            case '1':
                                 G_cformat.style = FMT_NUMERICAL;
                                 break;
                            case 'i':
                                 G_cformat.style = FMT_LROMAN;
                                 break;
                            case 'I':
                                 G_cformat.style = FMT_UROMAN;
                                 break;
                            default:
                                 break;
                          }
                          G_cformat.sep = *s2;
                          state = STATE_CHOICE;
                          sprintf(curr_choice, "%c%c", *s, *s2);
                          if ((s = next_fmt(choice_num,
                                            next_choice,
                                            G_cformat)) == NULL) {
                            fprintf(stderr, "Format problem\n");
                          }
                          p = s2 + 1;
                          while (isspace(*p)) {
                            p++;
                          }
                        } // Else still in the question
                        break;
                   default:
                        // Still in the question
                        break;
                 }  // End of switch
               } // Else doesn't match the format for a first choice
            }
            if (state == STATE_QUESTION) {
              // Still in a question
              maybe_correct = 0;  // Was a false hope
              if ((s = strcasestr(p, "</block>")) != NULL) {
                if (!in_block) {
                  fprintf(stderr, "*** WARNING *** %s - line %d ***"
                                  " </block> found while not in a block\n",
                                  fname, linenum);
                }
                in_block = 0;
                *s = '\0';
                // Concatenate p to question
                strbuf_add(&question, p);
                p = s + 8;
              }
              if (*p) {
                strbuf_add(&question, p);
              }
            } else {
              // We are in the first choice
              if (G_debug) {
                fprintf(stderr, "[%d] ", maybe_correct);
              }
              if (maybe_correct) {
                correct = 1;
                answer_known = 1;
                maybe_correct = 0;
              } 
              strbuf_add(&choice, p);
            }
            break;
          case STATE_CHOICE:
            // New choice or not?
            s = p;
            if (*s == '*') {
               maybe_correct = 1;
               s++;
            }
            if (G_debug) {
              fprintf(stderr, " -- Choice - expected: [%s] ",
                              next_choice);
            }
            if (strncmp(s, next_choice, strlen(next_choice)) == 0) {
              // Yes -- add the previous choice (curr_choice)
              (void)add_choice(&choices, &choice_cnt,
                               curr_choice, choice.s, correct);
              strbuf_clear(&choice);
              strcpy(curr_choice, next_choice);
              choice_num++;
              // Remove the label from the choice proper
              s += strlen(next_choice);
              p = s;
              while (isspace(*p)) {
                p++;
              }
              correct = 0;
              // Prepare the next choice
              if ((s = next_fmt(choice_num,
                                next_choice,
                                G_cformat)) == NULL) {
                fprintf(stderr, "Format problem\n");
              }
              if (G_debug) {
                fprintf(stderr, "[%d] ", maybe_correct);
              }
              if (maybe_correct) {
                correct = 1;
                answer_known = 1;
                maybe_correct = 0;
              } 
              strbuf_add(&choice, p);
            } else {
              // No, same old or perhaps an answer.
              maybe_correct = 0;
              // Answer ?
              if (strncasecmp(p, "answer", 6) == 0) {
                // Add the last choice
                (void)add_choice(&choices, &choice_cnt,
                                 curr_choice, choice.s, correct);
                strbuf_clear(&choice);
                state = STATE_ANSWER;
                p += 6;
                while (isspace(*p) || ispunct(*p)) {
                  p++;
                }
                if (*p) {
                  char *a = strtok(p, " \t,)-;.\n");
                  int   k;

                  while (a) {
                    if (strlen(a)) {
                      // Look for the identifier in the "choices" array
                      k = 0;
                      while ((k < choice_cnt)
                             && choices[k].id
                             && strcasecmp(a, choices[k].id)) {
                        k++;
                      }
                      if ((k < choice_cnt) 
                          && choices[k].id) {
                        choices[k].correct = 1;
                        answer_known = 1;
                      } else {
                        fprintf(stderr, "Failed to find correct answer [%s]/n",
                                        a);
                      }
                    }
                    a = strtok(NULL, " \t,)-;.");
                  }
                }
              } else {
                // Still the same choice
                strbuf_add(&choice, p);
              }
            }
            break;
          default:
            break;
        }
      } else {
        // Empty line
        if (!in_code && !in_block) {
          after_empty_line = 1;
          if ((state == STATE_ANSWER)
              || ((state == STATE_CHOICE)
                  && (answer_known || G_no_answers))) {
            if (state != STATE_ANSWER) {
              // Add the last choice
              (void)add_choice(&choices, &choice_cnt,
                               curr_choice, choice.s, correct);
            }
            eq = encode_question(question.s);
            // Process the question
            q = process_question(qnum,
                                 eq,
                                 choices,
                                 choice_cnt,
                                 ident);
            if (q) {
              strbuf_add(&xml, q);
              free(q);
            }
            if (eq) {
              free(eq);
            }
            clear_choices(choices, choice_cnt);
            strbuf_clear(&choice);
            strbuf_clear(&question);
            state = STATE_NONE;
          }
        } else {
          if (G_debug) {
            fprintf(stderr, "Empty line in %s",
                            (in_code ? "code block" : "block"));
          }
          strbuf_add(&question, "\n");
        }
      }
    }
    if (G_debug) {
      fprintf(stderr, "Freeing choices\n"); fflush(stderr);
    }
    if (choices) {
      clear_choices(choices, choice_cnt);
      free(choices);
      choices = NULL;
    }

    strbuf_dispose(&choice);
    strbuf_dispose(&question);
    strbuf_dispose(&code);
    if (G_debug) {
      fprintf(stderr, "Done\n"); fflush(stderr);
    }
  }
  return xml.s;
}

static void prepare_zip_qti_1_2(mz_zip_archive *pzip,
                                char           *manifestid,
                                char           *ident,
                                char           *title) {
   char  *m;

   if (pzip && ident) {
     // Create the manifest
     m = manifest_qti_1_2(manifestid, ident, title);
     if (m) {
       if (!mz_zip_writer_add_mem(pzip, "imsmanifest.xml",
                                  m, strlen(m),
                                  MZ_DEFAULT_COMPRESSION)) {
         free(m);
         fprintf(stderr, "Miniz error adding manifest\n");
         exit(1);
       }
       free(m);
     } else {
       fprintf(stderr, "Failed to create manifest\n");
       exit(1);
     }
   }
}

static void usage(char *progname) {
  fprintf(stderr, "Usage: %s [flags] filename [ fielname ... ]\n", progname);
  fprintf(stderr, "   or: %s [flags] < filename\n", progname);
}

int main(int argc, char **argv) {
    FILE           *fp;
    int             i;
    int             c;
    char            ident[IDENT_LEN];
    char            manifestident[IDENT_LEN];
    char            ident2[IDENT_LEN];
    char            timestamp[IDENT_LEN];
    MD5_CTX         md5ctx;  // For identifiers
    char            fname[FILENAME_MAX];
    char            zipname[FILENAME_MAX];
    char            archive_fname[FILENAME_MAX];
    char            title[FILENAME_MAX];
    char            buff[BUFFER_LEN];
    char           *p;
    char           *q;
    char           *s;
    time_t          now;
    struct tm      *t;
    mz_zip_archive  zip;
    mz_bool         status;
    struct stat     statbuf;
    STRBUF          xml;
    int             qnum = 0;

    title[0] = '\0';
    strbuf_init(&xml);
    now = time(NULL);
    while ((c = getopt(argc, argv, OPTIONS)) != -1) {
      switch (c) {
        case 'a':  // Answerless
          G_no_answers = 1;
          break;
        case 'm':  // Mixed formats
          G_mixed_format = 1;
          break;
        case 't': // Title
          strncpy(title, optarg, FILENAME_MAX);
          break;
        case 'd':  // Debug - also verbose
          G_debug = 1;
        case 'v':  // Verbose
          G_verbose = 1;
          break;
        case 'h':
        case '?':
        default:
          usage(argv[0]);
          return 0;
      }
    }
    if (G_debug) {
      fprintf(stderr, "argc = %d, optind = %d\n", argc, optind);
    }
    argc -= optind;
    argv += optind;
    if (strlen(title) == 0) {
      if ((t = localtime(&now)) != NULL) {
        sprintf(title, "Quiz %4d-%02d-%02d %02d:%02d",
                       1900 + t->tm_year,
                       1 + t->tm_mon,
                       t->tm_mday,
                       t->tm_hour,
                       t->tm_min);
      } else {
        strcpy(title, "Quiz");
      }
    }
    strncpy(zipname, title, FILENAME_MAX - 5);
    p = zipname;
    while (*p) {
      if (isspace(*p)) {
        *p = '_';
      }
      p++;
    }
    strcat(zipname, ".zip");
    // Initialize the zip writer
    memset(&zip, 0, sizeof(mz_zip_archive));
    status = mz_zip_writer_init_file(&zip, zipname, 0);
    if (!status) {
      fprintf(stderr, "Failed to initialize the zip writer\n");
      return -1;
    }

    // Initialize MD5
    MD5_Init(&md5ctx);
    // Beware, now the first argument of interest is
    // at index 0
    if (argc > 0) {
      // Compute the identifier as the MD5 checksum of parameters
      // (those that are OK)
      for (i = 0; i < argc; i++) {
        if (stat((const char *)argv[i], &statbuf) == 0) {
          MD5_Update(&md5ctx, argv[i],
                     (unsigned long)strlen(argv[i]));
        }
      }
      MD5_Final((unsigned char *)ident2, &md5ctx);
      strcpy(ident, "i");
      strcpy(manifestident, "m");
      for (i = 0; i < 16; i++) {
        sprintf(&ident[1+i*2], "%02x", (unsigned char)ident2[i]);
        sprintf(&manifestident[1+i*2], "%02x", (unsigned char)ident2[i]);
      }
      if (G_debug) {
        fprintf(stderr, "ident = %s\n", (char *)ident);
        fflush(stderr);
      }
      // Start preparing the zip file
      // Creates the manifest
      prepare_zip_qti_1_2(&zip, manifestident, ident, title);
      p = qti_1_2_header(ident, title);
      if (p) {
        strbuf_add(&xml, p);
        free(p);
      }
      for (i = 0; i < argc; i++) {
        if ((fp = fopen(argv[i], "r")) == NULL) {
          perror(argv[i]);
        } else {
          if (G_verbose) {
            fprintf(stderr, "-- Processing %s\n", argv[i]);
          }
          // Get the file base name and process it a bit
          strcpy(fname, argv[i]);
          if ((p = strrchr(fname, '/')) == NULL) {
            p = fname;
          } else {
            p++;
          }
          if ((q = strchr(p, '.')) != NULL) {
            *q = '\0';
          }
          q = p;
          while (*q) {
            if (isspace(*q)) {
              *q = '_';
            }
            q++;
          }
          s = process_file(fp, argv[i], &qnum, &zip, p);
          if (s) {
            strbuf_add(&xml, s);
            free(s);
          }
          fclose(fp);
        }
      }
    } else {
       if (G_verbose) {
         fprintf(stderr, "-- Reading from standard input\n");
       }
       // Create an identifier as "stdin" + timestamp
       if ((t = localtime(&now)) != NULL) {
         sprintf(timestamp, "stdin%4d%02d%02d%02d%02d%02d",
                            1900 + t->tm_year,
                            1 + t->tm_mon,
                            t->tm_mday,
                            t->tm_hour,
                            t->tm_min,
                            t->tm_sec);
         MD5_Update(&md5ctx, timestamp,
                    (unsigned long)strlen(timestamp));
         MD5_Final((unsigned char *)ident2, &md5ctx);
         strcpy(ident, "i");
         strcpy(manifestident, "m");
         for (i = 0; i < 16; i++) {
           sprintf(&ident[1+i*2], "%02x", (unsigned char)ident2[i]);
           sprintf(&manifestident[1+i*2], "%02x", (unsigned char)ident2[i]);
         }
         if (G_debug) {
           fprintf(stderr, "ident = %s\n", (char *)ident);
           fflush(stderr);
         }
         // Start preparing the zip file
         // Creates the manifest
         prepare_zip_qti_1_2(&zip, manifestident, ident, title);
         // Read from standard input
         s = process_file(stdin, "standard input", &qnum, &zip, "stdin");
         if (s) {
           strbuf_add(&xml, s);
           free(s);
         }
       } else {
         perror("localtime");
       }
    }
    sprintf(archive_fname, "%s.xml", ident);
    p = qti_1_2_footer();
    if (p) {
      strbuf_add(&xml, p);
      free(p);
    }
    status = mz_zip_writer_add_mem(&zip, archive_fname,
                                   xml.s, strlen(xml.s),
                                   MZ_DEFAULT_COMPRESSION);
    if (!status) {
      fprintf(stderr, "Failed to write XML file to zip archive\n");
      return -1;
    }
    // Close the zip writer
    (void)mz_zip_writer_finalize_archive(&zip);
    (void)mz_zip_writer_end(&zip);
    strbuf_dispose(&xml);
    return 0;
}
