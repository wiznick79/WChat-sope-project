/*
 * WChat Server v0.80 - 09/06/2019
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
	CLIENTS members;
	CLIENT *owner;
}CHATROOM;
// chatrooms vector
struct chatroom chatrooms[MAX_ROOMS];
int numofrooms;

void sendtoall(CLIENTS *clnts, PROTOCOL *protocol);					// sends msg to all connected clients
void sendprivate(CLIENTS *clnts, PROTOCOL *protocol, int connfd);	// sends msg to a specified client
void whoisonline(CLIENTS *clnts, PROTOCOL *protocol, int connfd);	// handles the /whoisonline requests
void user_on(CLIENTS *clnts, char *username);						// sends msg to all when a new client connects to server
void user_off(CLIENTS *clnts, PROTOCOL *protocol);					// sends msg to all when a client disconnects from server
CLIENT *insert_client(CLIENTS *clnts, char *username, char *cl_addr, int socket);	// inserts new client into the clients linked list
void remove_client(CLIENTS *clnts, int socket);						// removes a client from clients linked list
void changename(CLIENTS *clnts, PROTOCOL *protocol, int socket);	// changes the name of a client
void kickuser(CLIENTS *clnts, char *username, char *msg);			// kicks a client from the server
char* create_roomlist();
char* create_userlist(CLIENTS *clnts);
void send_userlist(CLIENTS *clnts);
void send_roomlist(CLIENTS *clnts);

int main(int argc, char **argv)
{
    char server_host[30];
    int maxfd, listenfd;
    fd_set allset;
    char buf[BUF_SIZE];
    char *msg = calloc(BUF_SIZE,sizeof(char));
    struct sockaddr_in cliaddr, servaddr;
    srand(time(NULL));
    CLIENTS clients={0,NULL};					// initialize the clients linked list
    numofrooms = 0;

    if (argc==2) strcpy(server_host,argv[1]);		// gets the server hostname from argv if specified
    else strcpy(server_host,"wiznick.ddns.net");	// else default server ip is wiznick.ddns.net

    listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);		// set the listening socket

    servaddr = init_server_info();

    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    listen(listenfd, QUEUE_SIZE);  // start listening

	char *timestamp = get_time();
	printf(GRN"%s Welcome to WChat Server v0.80"RESET"\n",timestamp);
    printf(RED"%s Server %s:%d started. Listening for incoming connections..."RESET"\n",timestamp,server_host,SERV_PORT);
	free(timestamp);
	
	numofrooms = 2;
	strcpy(chatrooms[0].name,"FCPorto");
	strcpy(chatrooms[1].name,"Olympiakos");

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
				else printf("%s Invalid command: %s",timestamp,msg);				
				free(timestamp);				
			}
			else {		// anything else is sent to all clients as a srvmsg 
				msg[strcspn(msg,"\n")]=0;		// remove the '\n' from the user input
				char *timestamp = get_time();
				char *js = calloc(BUF_SIZE,sizeof(char));				
				sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"srvmsg\", \"content\":\"%s\", \"timestamp\":\"%s\"}",msg,timestamp);	
				PROTOCOL *protocol=parse_message(js);  	// parse the json string 
				sendtoall(&clients,protocol);			// send the server message to everyone
				free(timestamp); free(js); free(protocol);
			}
			memset(msg,0,BUF_SIZE);
		}

        if (FD_ISSET(listenfd, &allset))		//check if something happens in the listenfd (new connection)
        {
            socklen_t clilen = sizeof(cliaddr);
            int connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);	// accept the connection
            char *timestamp = get_time();
            char *clad = inet_ntoa(cliaddr.sin_addr);
            printf("%s New incoming connection from IP: %s\n",timestamp,clad);
            read(connfd, buf, BUF_SIZE);		// read the file descriptor
			
            PROTOCOL *protocol=parse_message(buf);  // parse the buffer(buf) that is read from the socket
            
            if (strcmp(protocol->type,"registration")==0) {		// register the user
				CLIENT *newcl = malloc(sizeof(CLIENT));
				newcl = insert_client(&clients, protocol->source, clad, connfd);	// insert the new client into the clients linked list
				printf("%s User %s connected to the server, from %s, using socket %d.\n",timestamp,newcl->username,clad,newcl->client_socket);
				char *connmsg = calloc(70,sizeof(char));
				char *js = calloc(BUF_SIZE,sizeof(char));				
				sprintf(connmsg,"You are connected to the server %s.",server_host);
				sprintf(js,"{\"source\":\"Server\", \"target\":\"%s\", \"type\":\"welcome\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,connmsg,timestamp);
				write(connfd,js,strlen(js)+1);
				user_on(&clients, newcl->username);            // send msg to all clients that a new user has connected
				free(js); free(connmsg);
			}
			free(timestamp); free(protocol);
        }
        current=clients.first;
		while (current!=NULL) {
			int connfd = current->client_socket;		// check the sockets of the connected clients

			if (FD_ISSET(connfd, &allset)) {			// check if there is activity in one of them

				int bytes = read(connfd, buf, BUF_SIZE);	// if there is activity, read the file descriptor
				//write(1,buf,bytes); // debug msg
				if (bytes==0) {			// if a client disconnects, delete him from the clients list
					char *timestamp = get_time();
					printf("%s User %s disconnected from the server.\n",timestamp,current->username);
					free(timestamp);
					close(connfd);		// close the socket
					remove_client(&clients, connfd);	// remove the client from the clients linked list
				}
				else {
					PROTOCOL *protocol=parse_message(buf);  //parse the buffer(buf) that is read from the socket
					// check type of protocol received and call the correct function
					if (strcmp(protocol->type,"public")==0) sendtoall(&clients, protocol);									
					else if (strcmp(protocol->type,"private")==0) sendprivate(&clients, protocol, connfd);
					else if (strcmp(protocol->type,"whoisonline")==0) whoisonline(&clients, protocol, connfd);
					else if (strcmp(protocol->type,"user_on")==0) user_on(&clients, current->username);
					else if (strcmp(protocol->type,"user_off")==0) user_off(&clients, protocol);
					else if (strcmp(protocol->type,"changename")==0) changename(&clients,protocol,connfd);
					else if (strcmp(protocol->type,"userlist")==0) send_userlist(&clients);
					else if (strcmp(protocol->type,"roomlist")==0) send_roomlist(&clients);
					free(protocol);
				}
			}
			current=current->next;
		}
    }
}

// sends msgs of type public to all connected clients
void sendtoall(CLIENTS *clnts, PROTOCOL *protocol) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	printf("%s %s: %s\n",timestamp,protocol->source,protocol->content);		// echo msg on server console
	sprintf(js,"{\"source\":\"%s\", \"target\":\"everyone\", \"type\":\"public\", \"content\":\"%s\", \"timestamp\":\"%s\"}",protocol->source,protocol->content,protocol->timestamp);
	CLIENT *current = clnts->first;
	while (current!=NULL) {
		int socket = current->client_socket;
		write(socket,js,strlen(js)+1);
		current=current->next;
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
	//send_userlist(clnts);
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
	//send_userlist(clnts);
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
	char *list = calloc(numofrooms*MAX_ROOMS,sizeof(char)+1);
	sprintf(list,"Rooms(%d):",numofrooms);	
	for (int i=0; i<numofrooms; i++) {	
		strcat(list, chatrooms[i].name);
		strcat(list,",");
	}
	list[strlen(list)-1]='\0';	// replace the "," after last room with a '\0'
	return list;
}

char* create_userlist(CLIENTS *clnts) {
	char *list = calloc(clnts->numcl*30,sizeof(char)+1);
	CLIENT *current=clnts->first;
	sprintf(list,"Users(%d):",clnts->numcl);
	while (current!=NULL) {
		strcat(list, current->username);
		strcat(list,",");
		current = current->next;
	}
	list[strlen(list)-1]='\0';	// replace the "," after last name with a '\0'
	return list;
}

void send_userlist(CLIENTS *clnts) {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *ulist = create_userlist(clnts);
	char *timestamp = get_time();
	sprintf(js,"{\"source\":\"Server\", \"target\":\"everyone\", \"type\":\"userlist\", \"content\":\"%s\", \"timestamp\":\"%s\"}",ulist,timestamp);
	CLIENT *current=clnts->first;
	while (current!=NULL) {				// cycle through all connected clients
		int connfd = current->client_socket;
		write(connfd,js,strlen(js)+1);
		//printf("%s Sent userlist to %s.\n",timestamp,current->username); //debug msg
		current = current->next;
	}	
	free(timestamp); free(ulist); free(js);	
}

void send_roomlist(CLIENTS *clnts) {
	printf("roomlist requested\n");
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

