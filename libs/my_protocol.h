#ifndef MY_PROTOCOL_H_INCLUDED
#define MY_PROTOCOL_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "jsmn.h"
#define MAX_TOKENS 11

typedef struct protocol{
    char* type;
    char* source;
    char* target;
    char* content;
    char* timestamp;
}PROTOCOL;

char* get_token(jsmntok_t* t,char* message);
PROTOCOL* parse_message(char* message);
char* get_time();
char* strlwr(char *str);

#endif // MY_PROTOCOL_H_INCLUDED
