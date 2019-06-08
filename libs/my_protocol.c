#include "my_protocol.h"

char* get_token(jsmntok_t* t,char* message)
{
    int token_size=t->end-t->start;
    char* token=(char*)malloc(sizeof(char)*(token_size+1)); //allocate memory to the token
    strncpy(token,message+t->start,token_size); //copy token
    token[token_size]='\0'; //close the token
    return token;
}

PROTOCOL* parse_message(char* message){
    PROTOCOL* protocol=(PROTOCOL*)malloc(sizeof(PROTOCOL));    
    jsmntok_t tokens[MAX_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    jsmn_parse(&parser, message, strlen(message), tokens, MAX_TOKENS);
	
    for(int i=0;i<MAX_TOKENS;i++)
    {
        jsmntok_t* t=&tokens[i];
        if(t->end<=0) break;
        char* token=get_token(t,message);
        if(strcmp("type",token)==0)
        {
            free(token);
            i++;
            jsmntok_t* t=&tokens[i];
            char* token=get_token(t,message);
            protocol->type=(char*)malloc(sizeof(char)*(strlen(token)+1));
            strcpy(protocol->type,token);
        }
        else if(strcmp("source",token)==0)
        {
            free(token);
            i++;
            jsmntok_t* t=&tokens[i];
            char* token=get_token(t,message);
            protocol->source=(char*)malloc(sizeof(char)*(strlen(token)+1));
            strcpy(protocol->source,token);
        }
        else if(strcmp("target",token)==0)
        {
            free(token);
            i++;
            jsmntok_t* t=&tokens[i];
            char* token=get_token(t,message);
            protocol->target=(char*)malloc(sizeof(char)*(strlen(token)+1));
            strcpy(protocol->target,token);
        }
        else if(strcmp("content",token)==0)
        {
            free(token);
            i++;
            jsmntok_t* t=&tokens[i];
            char* token=get_token(t,message);
            protocol->content=(char*)malloc(sizeof(char)*(strlen(token)+1));
            strcpy(protocol->content,token);
        }
        else if(strcmp("timestamp",token)==0)
        {
            free(token);
            i++;
            jsmntok_t* t=&tokens[i];
            char* token=get_token(t,message);
            protocol->timestamp=(char*)malloc(sizeof(char)*(strlen(token)+1));
            strcpy(protocol->timestamp,token);            
        }
        else{
            free(token);
        }
    }
    return protocol;
}

char* get_time() {
	char* timestamp = malloc(sizeof(char)*40);
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	sprintf(timestamp,"[%02d/%02d/%04d %02d:%02d:%02d]",tm->tm_mday, tm->tm_mon+1, tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec);
	return timestamp;
}


char *strlwr(char *str) {
	char *tmp=malloc(sizeof(char)*(strlen(str)+1));
	strcpy(tmp,str);
	int len=strlen(tmp);
	for (int i=0; i<len;i++) {
		*(tmp+i) = tolower(*(tmp+i));
	}
	return tmp;
}
