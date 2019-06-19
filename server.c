/*
 * WChat Server v0.88 - 17/06/2019
 * by Nikolaos Perris (#36261) and Alvaro Magalhaes (#37000)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#include "libs/my_protocol.c"
#include "libs/my_protocol.h"
#include "libs/jsmn.c"
#include "libs/jsmn.h"

#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"
#define SERV_PORT 10007 /* arbitrary, but client and server must agree */
#define QUEUE_SIZE 10
#define BUF_SIZE 2048
#define MAX_CLIENTS 200
#define MAX_USERS 5
#define MAX_ROOMS 50

struct sockaddr_in init_server_info()
{
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);
    return servaddr;
}
// data structure for client
typedef struct client {
	int client_socket;
	char username[30];
	char cl_addr[20];
	char joined[50][30];
	struct client *next;
}CLIENT;

// data structure for clients
typedef struct clients {
	int numcl;
	CLIENT *first;
}CLIENTS;

// data structure for chatrooms
typedef struct chatroom {
	char name[30];
	int numofusers;	
	CLIENT members[MAX_USERS];
	CLIENT banned[50];
	CLIENT *owner;
}CHATROOM;
// chatrooms vector
struct chatroom chatrooms[MAX_ROOMS];
int numofrooms;
char server_host[30];
int maxfd, listenfd, cnfd;
fd_set allset;
pthread_t newconn,incmsg;
pthread_mutex_t mtx1,mtx2;
struct sockaddr_in cliaddr, servaddr;
char buf[BUF_SIZE];

void sendtoall(CLIENTS *clnts, PROTOCOL *protocol);					// sends msg to all connected clients
void sendprivate(CLIENTS *clnts, PROTOCOL *protocol, int connfd);	// sends msg to a specified client
void whoisonline(CLIENTS *clnts, PROTOCOL *protocol, int connfd);	// handles the /whoisonline requests
void user_on(CLIENTS *clnts, char *username);						// sends msg to all when a new client connects to server
void user_off(CLIENTS *clnts, PROTOCOL *protocol);					// sends msg to all when a client disconnects from server
CLIENT *insert_client(CLIENTS *clnts, char *username, char *cl_addr, int socket);	// inserts new client into the clients linked list
void remove_client(CLIENTS *clnts, int socket); // removes a client from clients linked list
void changename(CLIENTS *clnts, PROTOCOL *protocol, int socket);	// changes the name of a client
void kickuser(CLIENTS *clnts, char *username, char *msg);			// kicks a client from the server
char* create_roomlist();
char* create_userlist(CLIENTS *clnts, PROTOCOL *protocol);
void send_userlist(CLIENTS *clnts, PROTOCOL *protocol,int connfd);
void send_roomlist(CLIENTS *clnts);
void send_joinedlist(CLIENTS *clnts, int connfd);
void create_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd);
void join_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd);
void leave_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd);
void delete_room(CHATROOM *room, CLIENTS *clnts);
void ban_user_from_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd);
void kick_user_from_room(CLIENTS *clnts, PROTOCOL *protocol);
void *new_connection(void *cs);
void *incoming_msgs(void *cs);
void roomusers();

int main(int argc, char **argv)
{
    char *msg = calloc(BUF_SIZE,sizeof(char));    
    srand(time(NULL));
    CLIENTS clients={0,NULL};					// initialize the clients linked list
    numofrooms = 0; 
    pthread_mutex_init(&mtx1,NULL);
    pthread_mutex_init(&mtx2,NULL);

    if (argc==2) strcpy(server_host,argv[1]);		// gets the server hostname from argv if specified
    else strcpy(server_host,"wiznick.ddns.net");	// else default server ip is wiznick.ddns.net

    listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);		// set the listening socket

    servaddr = init_server_info();

    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    listen(listenfd, QUEUE_SIZE);  // start listening

	char *timestamp = get_time();
	printf(GRN"%s Welcome to WChat Server v0.88"RESET"\n",timestamp);
    printf(RED"%s Server %s:%d started. Listening for incoming connections..."RESET"\n",timestamp,server_host,SERV_PORT);
	free(timestamp);

    while (1)
    {
        FD_ZERO(&allset);			// clear all file descriptors
        FD_SET(listenfd, &allset);	// watch for new connections (add main socket file descriptor)
		FD_SET(0, &allset);			// watch for something from the keyboard
        maxfd = listenfd;        	// set listenfd as maxfd

		CLIENT *current=clients.first;		// cycle through all clients to add their sockets to the socket set
		while (current!=NULL) {
			int clsock = current->client_socket;
			FD_SET(clsock, &allset);
			if (clsock > maxfd) maxfd = clsock;
			current=current->next;
		}

        pselect(maxfd + 1, &allset, NULL, NULL, NULL, NULL); // blocks until something happens in the file descriptors watched

        if (FD_ISSET(0, &allset)) { 		// check if something happens in the stdin (new input from the keyboard)
			read(0, msg, BUF_SIZE);
			int len=strlen(msg);
			for (int i=0;i<len;i++) {
				if (msg[i]=='"' || msg[i]=='\\') msg[i]=' ';
			}					
			if (strncmp(msg,"/",1)==0) {			// if first char is "/", check if it's a correct command
				char *timestamp = get_time();
				if (strncmp(msg,"/quit",5)==0) {	// if /quit is written on the console, then exit server
					close(listenfd);
					exit(0);
				}				
				else if (strncmp(msg,"/clients",8)==0) {	// if /clients is written on the console, then it lists the connected clients
					printf("%s List of connected clients (%d):\n",timestamp,clients.numcl);					
					CLIENT *current=clients.first;
					int n=1;
					while (current!=NULL) {
						printf("%d. %s, ip: %s, socket: %d\n",n,current->username,current->cl_addr,current->client_socket);
						current=current->next;
						n++;
					}
				}
				else if (strncmp(msg,"/kick",5)==0) {
					int j=0;
					char *target = calloc(30,sizeof(char));
					for (int i=6;i<len;i++) {			// get the target's username from the /kick username message_here 
						if (*(msg+i)==' ' || *(msg+i)=='\n') {		// cycle stops when it finds the white space or the new line char
							target[j]='\0';
							break;
						}
						else {							// copy the target's name into the target variable
							target[j]=*(msg+i);
							j++;
						}
					}						
					char *tmp = calloc (BUF_SIZE,sizeof(char));
					strcpy(tmp,msg+j+7);			// set to tmp the correct beginning of the kick message
					tmp[strcspn(tmp,"\n")]=0;		// remove the '\n' from the end
					if (strcmp(tmp,"")==0) strcpy(tmp,"Kicked");
					kickuser(&clients,target,tmp);
					free(tmp); free(target);					
				}
				else if (strncmp(msg,"/roomusers",10)==0) roomusers();
				else printf("%s Invalid command: %s",timestamp,msg);				
				free(timestamp);				
			}
			else {		// anything else is sent to all clients as a srvmsg 
				msg[strcspn(msg,"\n")]=0;		// remove the '\n' from the user input
				char *timestamp = get_time();
				char *js = calloc(BUF_SIZE,sizeof(char));				
				sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"srvmsg\", \"content\":\"Server: %s\", \"timestamp\":\"%s\"}",msg,timestamp);	
				current=clients.first;
				while (current!=NULL) {
					write(current->client_socket,js,strlen(js)+1);
					current=current->next;
				}
				/*
				PROTOCOL *protocol=parse_message(js);  	// parse the json string 
				sendtoall(&clients,protocol);			// send the server message to everyone
				*/
				free(timestamp); free(js); //free(protocol);
			}
			memset(msg,0,BUF_SIZE);
		}

        if (FD_ISSET(listenfd, &allset)) {		//check if something happens in the listenfd (new connection)
            pthread_create(&newconn, NULL, new_connection, &clients);
            pthread_join(newconn,NULL);            
 		}
        current=clients.first;
		while (current!=NULL) {
			cnfd = current->client_socket;		// check the sockets of the connected clients
			if (FD_ISSET(cnfd, &allset)) {		// check if there is activity in one of them
				int bytes = read(cnfd, buf, BUF_SIZE);	// read the file descriptor
				//write(1,buf,bytes); // debug msg
				if (bytes==0) {			// if a client disconnects, delete him from the clients list
					char *timestamp = get_time();
					printf("%s User %s disconnected from the server.\n",timestamp,current->username);
					free(timestamp);
					close(cnfd);		// close the socket
					remove_client(&clients, cnfd);	// remove the client from the clients linked list						
				}
				else {			
					pthread_create(&incmsg, NULL, incoming_msgs, &clients);
					pthread_join(incmsg,NULL);
				}				
			}			
			current=current->next;
		}							
	}	
	pthread_join(newconn,NULL);
	pthread_join(incmsg,NULL);
	pthread_mutex_destroy(&mtx1);
	pthread_mutex_destroy(&mtx2);	    
}

void *new_connection(void *cs) {
	pthread_mutex_lock(&mtx1);
	CLIENTS *clnts = ((CLIENTS *)cs);
	socklen_t clilen = sizeof(cliaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);	// accept the connection
    char *timestamp = get_time();
    char *clad = inet_ntoa(cliaddr.sin_addr);
    printf("%s New incoming connection from IP: %s\n",timestamp,clad); 
    read(connfd, buf, BUF_SIZE);		// read the file descriptor
    PROTOCOL *protocol=parse_message(buf);  // parse the buffer(buf) that is read from the socket
            
    if (strcmp(protocol->type,"registration")==0) {		// register the user
		CLIENT *newcl = malloc(sizeof(CLIENT));
		newcl = insert_client(clnts, protocol->source, clad, connfd);	// insert the new client into the clients linked list
		printf("%s User %s connected to the server, from %s, using socket %d.\n",timestamp,newcl->username,clad,newcl->client_socket);
		char *connmsg = calloc(70,sizeof(char));
		char *js = calloc(BUF_SIZE,sizeof(char));				
		sprintf(connmsg,"You are connected to the server %s.",server_host);
		sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"welcome\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,connmsg,timestamp);
		write(connfd,js,strlen(js)+1);
		user_on(clnts, newcl->username);            // send msg to all clients that a new user has connected
		free(js); free(connmsg);
	}
	send_roomlist(clnts);
	pthread_mutex_unlock(&mtx1);
	free(timestamp); free(protocol);
	pthread_exit(0); 		// terminate the pthread
}

void *incoming_msgs(void *cs) {
	pthread_mutex_lock(&mtx1);
	CLIENTS *clnts = ((CLIENTS *)cs);
	PROTOCOL *protocol=parse_message(buf);  //parse the buffer(buf) that is read from the socket
	// check type of protocol received and call the correct function
	if (strcmp(protocol->type,"public")==0) sendtoall(clnts, protocol);									
	else if (strcmp(protocol->type,"private")==0) sendprivate(clnts, protocol, cnfd);
	else if (strcmp(protocol->type,"whoisonline")==0) whoisonline(clnts, protocol, cnfd);
	else if (strcmp(protocol->type,"user_on")==0) user_on(clnts, protocol->source);
	else if (strcmp(protocol->type,"user_off")==0) user_off(clnts, protocol);
	else if (strcmp(protocol->type,"changename")==0) changename(clnts,protocol,cnfd);
	else if (strcmp(protocol->type,"userlist")==0) send_userlist(clnts,protocol,cnfd);
	else if (strcmp(protocol->type,"roomlist")==0) send_roomlist(clnts);
	else if (strcmp(protocol->type,"create")==0) create_room(clnts,protocol,cnfd);
	else if (strcmp(protocol->type,"join")==0) join_room(clnts,protocol,cnfd);	
	else if (strcmp(protocol->type,"leave")==0) leave_room(clnts,protocol,cnfd);
	else if (strcmp(protocol->type,"ban")==0) ban_user_from_room(clnts,protocol,cnfd);
	free(protocol);
	pthread_mutex_unlock(&mtx1);	
	pthread_exit(0); 		// terminate the pthread
}

// sends msgs of type public to all connected clients
void sendtoall(CLIENTS *clnts, PROTOCOL *protocol) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	printf("%s %s #%s: %s\n",timestamp,protocol->source,protocol->target,protocol->content);		// echo msg on server console
	sprintf(js,"{\"source\":\"%s\", \"target\":\"%s\", \"type\":\"public\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,protocol->target,protocol->content,protocol->timestamp);
	
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(protocol->target,chatrooms[i].name)==0) {			
			for (int j=0;j<chatrooms[i].numofusers;j++) {
				int socket = chatrooms[i].members[j].client_socket;
				write(socket,js,strlen(js)+1);
			}		
		}	
	}
	free(timestamp); free(js);
}

// handles msgs of type private
void sendprivate(CLIENTS *clnts, PROTOCOL *protocol, int connfd) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	CLIENT *current = clnts->first;
	char *tmp = strlwr(protocol->target);
	char *ctmp;
	if (current!=NULL) ctmp = strlwr(current->username);
	
	while (current!=NULL && strcmp(tmp,ctmp)!=0) {
		free(ctmp);
		current = current->next;	// cycle through all clients to find the target client
		if (current!=NULL) ctmp = strlwr(current->username);
	}
			
	if (current!=NULL && strcmp(tmp,ctmp)==0) {		// if target is found, then send the msg to him
		int socket = current->client_socket;
		char *timestamp = get_time();
		printf("%s %s whispers to %s: %s\n",timestamp,protocol->source,current->username,protocol->content);
		sprintf(js,"{\"source\":\"%s\", \"target\":\"%s\", \"type\":\"private\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,current->username,protocol->content,protocol->timestamp);
		write(socket,js,strlen(js)+1);
		free(timestamp); free(js); free(ctmp); free(tmp);
		return;
	}
	
	// if target is not found, then send back to sender, that the specified user does not exist on server
	char *timestamp = get_time();
	printf("%s User %s does not exist.\n",timestamp,protocol->target);
	sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"User %s does not exist.\", \"timestamp\":\"%s\"}",protocol->source,protocol->target,timestamp);
	write(connfd,js,strlen(js)+1);
	free(timestamp); free(js); free(tmp);
}

// handles the /whoisonline requests
void whoisonline(CLIENTS *clnts, PROTOCOL *protocol, int connfd) {
	char *msg = calloc(BUF_SIZE,sizeof(char));
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	printf("%s User %s requested a whoisonline list.\n",timestamp,protocol->source);	// echo on server console that a client made the request
	// create the list
	sprintf(msg,"Online users (%d): ",clnts->numcl);
	CLIENT *current=clnts->first;
	while (current!=NULL) {
		strcat(msg, current->username);
		strcat(msg,", ");
		current = current->next;
	}
	msg[strlen(msg)-2]='\0';	// replace the "," after last name with a '\0'
	sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"whoisonline\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,msg,timestamp);
	write(connfd,js,strlen(js)+1);	// send the connected clients list
	free(timestamp); free(msg); free(js);
}

// sends a msg to all users that a new client has connected to the server
void user_on(CLIENTS *clnts, char *username) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	char *tmp = strlwr(username);
	// create msg to be sent
	sprintf(js,"{\"source\":\"%s\", \"target\":\"everyone\", \"type\":\"user_on\", \"content\":\"null\", \"timestamp\":\"%s\"}",username,timestamp);
	CLIENT *current=clnts->first;
	while (current!=NULL) {				// cycle through all connected clients
		char *ctmp = strlwr(current->username);
		if (strcmp(ctmp,tmp)!=0) {		// except the client that just connected
			int connfd = current->client_socket;		
			write(connfd,js,strlen(js)+1);
		}
		free(ctmp);
		current = current->next;
	}	
	free(timestamp); free(tmp); free(js);
}

// sends a msg to all users that a client has disconnected from the server
void user_off(CLIENTS *clnts, PROTOCOL *protocol) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *tmp = strlwr(protocol->source);
	sprintf(js,"{\"source\":\"%s\", \"target\":\"everyone\", \"type\":\"user_off\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,protocol->content,protocol->timestamp);
	CLIENT *current=clnts->first;
	while (current!=NULL) {				// cycle through all connected clients
		char *ctmp = strlwr(current->username);
		if (strcmp(ctmp,tmp)!=0) {		// except the client that just disconnected
			int connfd = current->client_socket;
			write(connfd,js,strlen(js)+1);
		}
		free(ctmp);
		current = current->next;
	}	
	free(tmp); free(js);
}

CLIENT *insert_client(CLIENTS *clnts, char *username, char *cl_addr, int socket) {
	CLIENT *temp=malloc(sizeof(CLIENT));
	strcpy(temp->username,username);
	temp->client_socket=socket;
	strcpy(temp->cl_addr,cl_addr);
	temp->next=NULL;
	CLIENT *current=clnts->first, *previous;
	char *timestamp = get_time();	
	char *tmp = strlwr(username);
	
	if (strcmp(tmp,"server")==0) {				// handle the special case someone tries to use name "server"
		int r = rand()%900 + 100;				// by assigning a new username "Guest"+random number(100-999)
		char* tname = calloc(30,sizeof(char));
		char* msg = calloc(BUF_SIZE,sizeof(char));
		char* js = calloc(BUF_SIZE,sizeof(char));
		sprintf(tname,"Guest%d",r);		// append a random number (100 to 999) to the username
		sprintf(msg,"Username %s is not allowed. You have been assigned username %s.",username,tname);
		sprintf(js,"{\"source\":\"server\", \"target\":\"%s\", \"type\":\"changename\", \"content\":\"%s\", \"timestamp\":\"%s\"}",tname,msg,timestamp);
		write(socket,js,strlen(js)+1);				// send a json string to the client, of type changename, to force client to change the username 
		free(timestamp); free(msg); free(js);
		return insert_client(clnts,tname,cl_addr,socket);	// insert the new username into the linked list
	}	
	char *ctmp;
	if (current!=NULL) ctmp = strlwr(current->username);	
	if (current==NULL || (strcmp(tmp,ctmp)<0)) {			// handle the head case
		temp->next=clnts->first;
		clnts->first=temp;
		clnts->numcl++;
		free(timestamp); free(tmp);
		if (current!=NULL) free(ctmp);		
		return temp;
	}		
	while (current->next!=NULL && (strcmp(ctmp,tmp)<0)) { 	// find the correct position
			free(ctmp);
			previous=current;
			current=current->next;
			ctmp = strlwr(current->username);
	}	
	if (current->next==NULL && (strcmp(ctmp,tmp)<0)) {  	// handle the tail case
		temp->next=NULL;
		current->next=temp;
	}
	else if (strcmp(tmp,ctmp)==0) {				// handle the case the username already exists
			int r = rand()%900 + 100;			// by assigning a new semi-random username
			char* tname = calloc(30,sizeof(char));
			char* msg = calloc(BUF_SIZE,sizeof(char));
			char* js = calloc(BUF_SIZE,sizeof(char));
			sprintf(tname,"%s%d",username,r);				// append a random number (100 to 999) to the username
			printf("%s User %s has been assigned the username %s.\n",timestamp,username,tname);
			sprintf(msg,"Username %s is taken. You have been assigned username %s.",username,tname);
			sprintf(js,"{\"source\":\"server\", \"target\":\"%s\", \"type\":\"changename\", \"content\":\"%s\", \"timestamp\":\"%s\"}",tname,msg,timestamp);
			write(socket,js,strlen(js)+1);				// send a json string to the client, of type changename, to force client to change the username 
			free(timestamp); free(msg); free(js); free(ctmp); free(tmp);
			return insert_client(clnts,tname,cl_addr,socket);	// insert the new username into the linked list
		}
	else {						// handle the case it's between our previous and current
		previous->next=temp;	// link previous to temp
		temp->next=current;		// link temp to current
	}
	clnts->numcl++;		// increase number of clients
	free(ctmp); free(tmp); free(timestamp);
	return temp;		// return the new client
}

void remove_client(CLIENTS *clnts, int socket) {
	
	for (int i=0;i<numofrooms;i++) {
		for (int j=0;j<chatrooms[i].numofusers;j++) {
			if (socket==chatrooms[i].members[j].client_socket) {
				for (int k=j; k<chatrooms[i].numofusers;k++) {
					chatrooms[i].members[k]=chatrooms[i].members[k+1];
				}
				chatrooms[i].numofusers--;
				chatrooms[i].owner=&(chatrooms[i].members[0]);
				if (chatrooms[i].numofusers==0) delete_room(&chatrooms[i],clnts);
			}		
		}
	}
	
	if (socket==clnts->first->client_socket) {
		CLIENT *cdel = clnts->first;
		clnts->first = cdel->next;
		clnts->numcl--;
		free(cdel);		
		return;
	}
	CLIENT *previous=clnts->first, *current=clnts->first->next;
	while (current!=NULL) {
		if (socket==current->client_socket) {
			previous->next=current->next;
			clnts->numcl--;
			free(current);
			return;
		}
		previous=current;
		current=current->next;
	}
}

void changename(CLIENTS *clnts, PROTOCOL *protocol, int socket) {
	char *tmp = (char*) calloc(30,sizeof(char));
	char *msg = (char*) calloc(BUF_SIZE,sizeof(char));
	char *js = (char*) calloc(BUF_SIZE,sizeof(char));
	char *taddr = (char*) calloc(20,sizeof(char));
	char *timestamp = (char*) get_time();
	CLIENT *current=clnts->first;
	while (current!=NULL) {
		if (socket==current->client_socket) {
			strcpy(taddr,current->cl_addr);
		}
		current=current->next;
	}
	strcpy(tmp,protocol->source);
	remove_client(clnts,socket);			// remove the client from the clients linked list
	CLIENT *rencl = malloc(sizeof(CLIENT));
	rencl = insert_client(clnts, protocol->content, taddr, socket);	// insert the renamed client back into the clients linked list
	sprintf(msg,"%s is now known as %s.",tmp,rencl->username);
	printf("%s %s\n",timestamp,msg);
	sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",msg,timestamp);
	current=clnts->first;
	while (current!=NULL) {		// cycle through all connected clients
		int connfd = current->client_socket;
		write(connfd,js,strlen(js)+1);
		current = current->next;
	}
	free(timestamp); free(tmp); free(msg); free(js);
}

void kickuser(CLIENTS *clnts, char *username, char *msg) {
	char *timestamp = get_time();
	if (clnts->numcl==0) {
		printf("%s No users are connected to the server.\n",timestamp);
		free(timestamp);
		return;
	}		
	char *js = calloc(BUF_SIZE,sizeof(char));	
	char *tmp = strlwr(username);
	CLIENT *current=clnts->first;
	while (current!=NULL) {		// cycle through all connected clients
		char *ctmp = strlwr(current->username);
		if (strcmp(ctmp,tmp)==0) {
			int socket = current->client_socket;
			sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"kick\", \"content\":\"You have been kicked from the server (%s).\", \"timestamp\":\"%s\"}",username,msg,timestamp);
			write(socket,js,strlen(js)+1);
			remove_client(clnts,socket);
			close(socket);			
			printf("%s You kicked user %s from the server (%s).\n",timestamp,username,msg);			
			// broadcast a kick user message
			sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"srvmsg\", \"content\":\"User %s has been kicked from the server (%s).\", \"timestamp\":\"%s\"}",username,msg,timestamp);
			CLIENT *temp=clnts->first;
			while (temp!=NULL) {		// cycle through all connected clients
				int connfd = temp->client_socket;
				write(connfd,js,strlen(js)+1);
				temp = temp->next;
			}
			free(ctmp);
			break;
		}
		free(ctmp);
		current = current->next;
	}
	if (current==NULL) printf("%s User %s does not exist.\n",timestamp,username);
	free(timestamp); free(js); free(tmp);
}

char* create_roomlist() {
	char *list = calloc(numofrooms*30,sizeof(char)+1);
	sprintf(list,"Rooms(%2d):",numofrooms);	
	for (int i=0; i<numofrooms; i++) {	
		strcat(list, chatrooms[i].name);
		strcat(list,",");
	}
	list[strlen(list)-1]='\0';	// replace the "," after last room with a '\0'
	return list;
}

char* create_userlist(CLIENTS *clnts, PROTOCOL *protocol) {
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(protocol->content,chatrooms[i].name)==0) {
			char *list = calloc(chatrooms[i].numofusers*30,sizeof(char)+1);	
			sprintf(list,"Users(%2d):",chatrooms[i].numofusers);
			for (int j=0;j<chatrooms[i].numofusers;j++) {
				strcat(list, chatrooms[i].members[j].username);
				if (j<chatrooms[i].numofusers-1) strcat(list,",");
			}
			return list;
		}
	}	
	return NULL;
}

void send_userlist(CLIENTS *clnts, PROTOCOL *protocol, int connfd) {	
	char *js = calloc(BUF_SIZE,sizeof(char));	
	char *ulist = create_userlist(clnts,protocol);	
	char *timestamp = get_time();
	CLIENT *current=clnts->first;
	while (current!=NULL) {				// cycle through all connected clients
		if (connfd==current->client_socket) break;
		current = current->next;
	}		
	sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"userlist\", \"content\":\"%s\", \"timestamp\":\"%s\"}",current->username,ulist,timestamp);
	write(connfd,js,strlen(js)+1);
	printf("%s Sent userlist for room %s to %s.\n",timestamp,protocol->content,current->username); //debug msg
	free(timestamp); free(ulist); free(js);
}

void send_roomlist(CLIENTS *clnts) {
	//printf("roomlist requested\n");
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *rlist = create_roomlist();
	char *timestamp = get_time();		
	sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"roomlist\", \"content\":\"%s\", \"timestamp\":\"%s\"}",rlist,timestamp);
	CLIENT *current=clnts->first;
	while (current!=NULL) {				// cycle through all connected clients
		int connfd = current->client_socket;
		write(connfd,js,strlen(js)+1);
		printf("%s Sent roomlist %s to %s.\n",timestamp,rlist,current->username); //debug msg
		current = current->next;
	}		
	free(timestamp); free(rlist); free(js);	
}

void send_joinedlist(CLIENTS *clnts, int connfd) {
	char *timestamp = get_time();
	char *jlist = calloc(numofrooms*30,sizeof(char)+1);
	char *js = calloc(BUF_SIZE,sizeof(char));
	sprintf(jlist,"Rooms:");	
	CLIENT *current=clnts->first;
	while (current!=NULL) {	
		if (connfd==current->client_socket) {
			int k=0;
			while (strcmp(current->joined[k],"\0")!=0) {
				strcat(jlist,current->joined[k]);
				strcat(jlist,",");
				k++;
			}
			sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"joinedlist\", \"content\":\"%s\", \"timestamp\":\"%s\"}",current->username,jlist,timestamp);
			write(connfd,js,strlen(js)+1);
			printf("%s Sent joinedlist %s to %s.\n",timestamp,jlist,current->username); //debug msg
		}
		current=current->next;
	}
	free(timestamp);free(jlist);free(js);
}

void create_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd) {	
	char *timestamp = get_time();
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(protocol->content,chatrooms[i].name)==0) {
			printf("%s Chatroom #%s already exists\n",timestamp,protocol->content);
			free(timestamp);
			return;
		}
	}
	CHATROOM *room = malloc(sizeof(CHATROOM));
	strcpy(room->name,protocol->content);
	room->numofusers=1;
	CLIENT *current=clnts->first;
	while (current!=NULL) {	
		if (connfd==current->client_socket) {
			room->members[0]=*current;
			room->owner=current;
			break;
		}
		current = current->next;
	}	
	chatrooms[numofrooms]=*room;
	numofrooms++;
	int k=0;
	while (strcmp(current->joined[k],"\0")!=0) k++;
	strcpy(current->joined[k],room->name);
	printf("%s User %s created room #%s\n",timestamp,protocol->source,protocol->content);
	send_joinedlist(clnts,connfd);
	free(timestamp);
}
	
void join_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd) {	
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *str = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	CLIENT *current=clnts->first;
	while (current!=NULL) {	
		if (connfd==current->client_socket) break;
		current = current->next;
	}
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(protocol->content,chatrooms[i].name)==0) {
			for (int j=0;j<chatrooms[i].numofusers;j++) {
				if (strcmp(protocol->source,chatrooms[i].members[j].username)==0) {
					printf("%s User %s is already in room #%s\n",timestamp,protocol->source,protocol->content);
					sprintf(str,"You are already in room #%s",protocol->content);	
					sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
					write(connfd,js,strlen(js)+1);
					free(timestamp);free(str);free(js);
					return;
				}
			}
			if (chatrooms[i].numofusers<5) {
				chatrooms[i].members[chatrooms[i].numofusers]=*current;
				chatrooms[i].numofusers++;
				printf("%s User %s joined room #%s\n",timestamp,protocol->source,protocol->content);
				sprintf(str,"You have joined room #%s.",protocol->content);	
				sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
				write(connfd,js,strlen(js)+1);
				int k=0;
				while (strcmp(current->joined[k],"\0")!=0) k++;
				strcpy(current->joined[k],chatrooms[i].name);
				send_joinedlist(clnts,connfd);
				k=0;
				while (strcmp(chatrooms[i].members[k].username,"\0")!=0) {
					if (chatrooms[i].members[k].client_socket!=connfd) {
						sprintf(str,"User %s has joined room #%s.",protocol->source,protocol->content);
						sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",chatrooms[i].members[k].username,str,timestamp);
						write(chatrooms[i].members[k].client_socket,js,strlen(js)+1);
						send_userlist(clnts,protocol,chatrooms[i].members[k].client_socket);
					}
					k++;
				}
				free(timestamp);free(str);free(js);
				return;
			}
			else {
				printf("User %s trying to join full room #%s\n",protocol->source,protocol->content);
				sprintf(str,"Room #%s is full, you can't join.",protocol->content);	
				sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
				write(connfd,js,strlen(js)+1);
				free(timestamp);free(str);free(js);
				return;
			}			
		}		
	}
	printf("User %s trying to join room #%s, but it doesn't exist\n",protocol->source,protocol->content);
	sprintf(str,"Room %s does not exist",protocol->content);	
	sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
	write(connfd,js,strlen(js)+1);
	free(timestamp);free(str);free(js);	
}

void leave_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *str = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	CLIENT *current=clnts->first;
	while (current!=NULL) {	
		if (connfd==current->client_socket) break;
		current = current->next;
	}		
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(protocol->content,chatrooms[i].name)==0) {
			for (int j=0;j<chatrooms[i].numofusers;j++) {
				if (strcmp(protocol->source,chatrooms[i].members[j].username)==0) {
					for (int k=j; k<chatrooms[i].numofusers;k++) {
						chatrooms[i].members[k]=chatrooms[i].members[k+1];
					}
					chatrooms[i].numofusers--;
					chatrooms[i].owner=&(chatrooms[i].members[0]);
					if (chatrooms[i].numofusers==0) delete_room(&chatrooms[i],clnts);
					printf("%s User %s left from room #%s\n",timestamp,protocol->source,protocol->content);
					sprintf(str,"You have left from room #%s.",protocol->content);	
					sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
					write(connfd,js,strlen(js)+1);
					int k=0;
					while (strcmp(current->joined[k],protocol->content)!=0) k++;
					while (strcmp(current->joined[k],"\0")!=0) {
						strcpy(current->joined[k],current->joined[k+1]);
						k++;
					}					
					send_joinedlist(clnts,connfd);
					k=0;
					while (strcmp(chatrooms[i].members[k].username,"\0")!=0) {
						if (chatrooms[i].members[k].client_socket!=connfd) {
							sprintf(str,"User %s has left from room #%s.",protocol->source,protocol->content);
							sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",chatrooms[i].members[k].username,str,timestamp);
							write(chatrooms[i].members[k].client_socket,js,strlen(js)+1);
							send_userlist(clnts,protocol,chatrooms[i].members[k].client_socket);
						}
						k++;
					}					
					free(timestamp);free(str);free(js);					
					return;
				}
			}
		}
	}
	sprintf(str,"You haven't joined room %s.",protocol->content);	
	sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,str,timestamp);
	write(connfd,js,strlen(js)+1);
	free(timestamp);free(str);free(js);
}

void delete_room(CHATROOM *room, CLIENTS *clnts) {
	for (int i=0;i<numofrooms;i++) {
		if (strcmp(room->name,chatrooms[i].name)==0) {
			for (int j=i;j<numofrooms;j++) {
				chatrooms[j]=chatrooms[j+1];
			}
			numofrooms--;
		}
	}
	send_roomlist(clnts);
}

void ban_user_from_room(CLIENTS *clnts, PROTOCOL *protocol,int connfd) {
	char *js = calloc(BUF_SIZE,sizeof(char));	
	char *timestamp = get_time();
	
	printf("%s User %s trying to ban user %s from room #%s\n",timestamp,protocol->source,protocol->content,protocol->target);
	
	
	CLIENT *current=clnts->first;
	while (current!=NULL) {	
		if (strcmp(protocol->content,current->username)==0) break;
		current = current->next;
	}	

	for (int i=0;i<numofrooms;i++) {
		if (strcmp(chatrooms[i].name,protocol->target)==0) {
			if (strcmp(chatrooms[i].owner->username,protocol->source)==0) {	// user is room moderator, so he can ban
				for (int j=0;j<chatrooms[i].numofusers;j++) {
					if (strcmp(chatrooms[i].members[j].username,protocol->content)==0) {
						for (int k=j; k<chatrooms[i].numofusers;k++) {
							chatrooms[i].members[k]=chatrooms[i].members[k+1];
						}
						chatrooms[i].numofusers--;
						int l=0;
						while (strcmp(chatrooms[i].banned[l].username,"\0")!=0) l++;
						chatrooms[i].banned[l]=*current;
						int m=0;
						while (strcmp(current->joined[m],protocol->target)!=0) m++;
						while (strcmp(current->joined[m],"\0")!=0)
							strcpy(current->joined[m],current->joined[m+1]);
						send_joinedlist(clnts,current->client_socket);
						printf("%s User %s got banned from room #%s\n",timestamp,protocol->content,protocol->target);
						sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"You have banned %s from room #%s.\", \"timestamp\":\"%s\"}",protocol->source,protocol->content,protocol->target,timestamp);   
						write(connfd, js, strlen(js)+1);
						sprintf(js,"{\"source\":\"%s\", \"target\":\"%s\", \"type\":\"ban\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,protocol->target,protocol->content,timestamp);   
						write(current->client_socket, js, strlen(js)+1);
						free(timestamp);free(js);					
					}
				}
			}
			else { // user is not room moderator , he can't ban other users
				sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"srvmsg\", \"content\":\"You don't have moderator rights to ban users from room #%s.\", \"timestamp\":\"%s\"}",protocol->source,protocol->target,timestamp);   
				write(connfd, js, strlen(js)+1);
				free(timestamp); free(js);
			}
		}		
	}
			
	
}

void roomusers() {
	printf("There are %d rooms created in the server.\n",numofrooms);
	for (int i=0;i<numofrooms;i++) {
		printf("Room %s (%d): \n",chatrooms[i].name,chatrooms[i].numofusers);
		printf("Owner: %s , Members : ",chatrooms[i].owner->username);
		for (int j=0;j<chatrooms[i].numofusers;j++) {
			 printf("%s ",chatrooms[i].members[j].username);
			 if (j<chatrooms[i].numofusers-1) printf(",");
		}
		printf("\n");
	}
}
