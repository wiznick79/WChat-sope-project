/*
 * Chat Client v0.86 (pthread+gtk version)- 12/06/2019 
 * by Nikolaos Perris (#36261) and Alvaro Magalhaes (#37000)
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <gtk/gtk.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

#include "libs/my_protocol.c"
#include "libs/my_protocol.h"
#include "libs/jsmn.c"
#include "libs/jsmn.h"

#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define SERVER_PORT 10007		/* arbitrary, but client and server must agree */
#define BUF_SIZE 2048			/* block transfer size */

GtkWidget *window;
GtkWidget *grid;
GtkWidget *view;
GtkWidget *userlist, *roomlist;
GtkWidget *vpaned;
GtkWidget *combobox;
GtkWidget *cr_room;
GtkWidget *statusbar;
GtkListStore *umodel, *rmodel;
GtkTreeIter useriter, roomiter;

void *listenforincoming(void *s);		// function that listens for incoming messages
void *sendmessages(void *s);
void whoisonline();
void update_user_list(char *userslist);
void update_room_list(char *roomslist);
void request_user_list();
void request_room_list();
void sendusermsg(const gchar *gmsg);
void create_room_request();
void create_room(const gchar *roomname);
void join_room(const gchar *roomname);
void leave_room(const gchar *roomname);
void quit_program();
void help_window();
void about_window();
void connect_to_srv(GtkWidget *entry, gpointer dialog);

int c;	
struct sockaddr_in channel;		/* holds IP address */ 
char server_ip[30];
char username[30];
char users[100][30];
char rooms[50][30];
char jroom[30];
char *args[3];
int srv_sock,numofrooms;
pthread_t snd,rcv;  

struct sockaddr_in init_client_info(char* name) {
    struct hostent *h; /* info about server */
    h = gethostbyname(name);		/* look up host's IP address */
    if (!h) {
        perror("gethostbyname");
        exit(-1);
    }
    struct sockaddr_in channel;
    memset(&channel, 0, sizeof(channel));
    channel.sin_family= AF_INET;
    memcpy(&channel.sin_addr.s_addr, h->h_addr, h->h_length);
    channel.sin_port= htons(SERVER_PORT);
    return channel;
}

GdkPixbuf *create_pixbuf(const gchar * filename) {    
   GdkPixbuf *pixbuf;
   GError *error = NULL;
   pixbuf = gdk_pixbuf_new_from_file(filename, &error);   
   if (!pixbuf) {       
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
   }
   return pixbuf;
}

static GtkWidget *create_roomlist() {
	GtkWidget *scrolled_window;
    GtkWidget *rooms_tree;
    GtkCellRenderer *cell;
    GtkTreeViewColumn *column;
    
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
                                    
    rmodel = gtk_list_store_new (1, G_TYPE_STRING);
    rooms_tree = gtk_tree_view_new ();
    gtk_container_add (GTK_CONTAINER (scrolled_window), rooms_tree);
    gtk_tree_view_set_model (GTK_TREE_VIEW (rooms_tree), GTK_TREE_MODEL (rmodel));
    gtk_widget_show (rooms_tree);
    
    // Add the roomlist to the window 
    for (int i = 0; i < numofrooms; i++) { 
		gchar *rmsg = g_strdup_printf ("%s", rooms[i]);
        gtk_list_store_append (GTK_LIST_STORE (rmodel), &roomiter);
        gtk_list_store_set (GTK_LIST_STORE (rmodel), &roomiter, 0, rmsg, -1);
        g_free (rmsg);
    }
    
    cell = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Chatrooms:", cell, "text", 0, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (rooms_tree), GTK_TREE_VIEW_COLUMN (column));

    return scrolled_window;
}

static GtkWidget *create_userlist() {	
    GtkWidget *scrolled_window;
    GtkWidget *users_tree;
    GtkCellRenderer *cell;
    GtkTreeViewColumn *column;

    /* Create a new scrolled window, with scrollbars only if needed */
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);

    umodel = gtk_list_store_new (1, G_TYPE_STRING);
    users_tree = gtk_tree_view_new ();
    gtk_container_add (GTK_CONTAINER (scrolled_window), users_tree);
    gtk_tree_view_set_model (GTK_TREE_VIEW (users_tree), GTK_TREE_MODEL (umodel));
    gtk_widget_show (users_tree);
	
    // Add the userlist to the window 
    for (int i = 0; i < 10; i++) { 
		gchar *umsg = g_strdup_printf ("%s", users[i]);
        gtk_list_store_append (GTK_LIST_STORE (umodel), &useriter);
        gtk_list_store_set (GTK_LIST_STORE (umodel), &useriter, 0, umsg, -1);
        g_free (umsg);
    }
    cell = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Online users:", cell, "text", 0, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (users_tree), GTK_TREE_VIEW_COLUMN (column));

    return scrolled_window;
}

static void connect_dialog() {
	GtkWidget *dialog,*dlabel,*dentry,*darea;
	GtkDialogFlags flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT;
    dialog = gtk_dialog_new_with_buttons("Choose your username",GTK_WINDOW(window),flags,"OK",GTK_RESPONSE_OK,"Cancel",GTK_RESPONSE_CANCEL,NULL);
    darea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    dlabel = gtk_label_new("Type your username:");
    gtk_container_add(GTK_CONTAINER(darea),dlabel);
    dentry = gtk_entry_new();
	g_signal_connect(dentry, "activate", G_CALLBACK(connect_to_srv), dialog);
    gtk_container_add(GTK_CONTAINER(darea),dentry);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {		
		connect_to_srv(dentry,dialog);		
	}
    else 
		gtk_widget_destroy(dialog);
}

void connect_to_srv(GtkWidget *entry, gpointer dialog) {
	numofrooms=0;
	if (strcmp(gtk_entry_get_text(GTK_ENTRY(entry)),"")!=0) {			
		const gchar *str = gtk_entry_get_text(GTK_ENTRY(entry));
		strcpy(username,str);			
		int k=0;
		while (username[k]!='\0') {			// loop to replace all non-alphanumeric chars with an underscore
			if (isalnum(username[k])==0) username[k]='_';
			k++;
		}
		gtk_widget_destroy(dialog);					
	}
	else {		
		connect_dialog();
	}
			 
    srv_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv_sock < 0) {
        perror("Cannot create socket");
        exit(-1);
    }
        	    
    channel = init_client_info(server_ip);
       
    c = connect(srv_sock, (struct sockaddr *) &channel, sizeof(channel));
    if (c < 0) {
        perror("Cannot connect to server");
        exit(-1);
    }  
    
    pthread_create(&rcv, NULL, listenforincoming, NULL); // create thread rcv to receive messages, using function listenforincoming
	pthread_create(&snd, NULL, sendmessages, NULL);	// create thread snd to send messages, using function sendmessages
    
    char *js = calloc(BUF_SIZE,sizeof(char));
    char *timestamp = get_time();   
    // create json string of type registration and sends it to the server
    sprintf(js,"{\"source\":\"%s\", \"target\":\"server\", \"type\":\"registration\", \"content\":\"null\", \"timestamp\":\"%s\"}",username,timestamp);
    write(srv_sock, js, strlen(js)+1);
	free(timestamp); free(js);		
}

static void insert_text(GtkTextBuffer *buffer, const gchar *entry_text) {
    GtkTextIter txtiter;
    GtkAdjustment *adjustment;

    gtk_text_buffer_get_iter_at_offset(buffer, &txtiter, gtk_text_buffer_get_char_count(buffer));

    if (gtk_text_buffer_get_char_count(buffer))
        gtk_text_buffer_insert (buffer, &txtiter, "\n", 1);

    gtk_text_buffer_insert (buffer, &txtiter,entry_text, -1);
    adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
}

static void enter_callback(GtkWidget *widget, GtkWidget *entry) {
    const gchar *entry_text;
    entry_text = gtk_entry_get_text(GTK_ENTRY(entry));
    
    //GtkTextBuffer *buffer;    
    //buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

    //insert_text(buffer, entry_text);   
    sendusermsg(entry_text);    
    gtk_entry_set_text (GTK_ENTRY (entry), "");   
}

/* Create a scrolled text area that displays a "message" */
static GtkWidget *create_text(void) {
    GtkWidget *scrolled_window;
    //GtkTextBuffer *buffer;
    view = gtk_text_view_new ();
    //buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_container_add (GTK_CONTAINER (scrolled_window), view);
    gtk_widget_show_all (scrolled_window);
    return scrolled_window;
}

int main(int argc, char *argv[])
{
    GtkWidget *box;    
    GtkWidget *entry;
    GtkWidget *create_btn,*join_btn;       
    GtkWidget *text;
    GtkWidget *menubar;
    GtkWidget *menufile;
    GtkWidget *menuhelp;
    GtkWidget *filesubmenu;
    GtkWidget *helpsubmenu;
    GtkWidget *item_quit;
    GtkWidget *item_connect;
    GtkWidget *item_disconnect;
    GtkWidget *item_help;
    GtkWidget *item_about;     
    
    args[0] = malloc(sizeof(char)*(strlen(argv[0])+1));    
    strcpy(args[0],argv[0]);
	if (argv[1]!=NULL) {
		args[1] = malloc(sizeof(char)*(strlen(argv[1])+1));
		strcpy(args[1],argv[1]);
		args[2]=NULL;
	}
    else args[1]=NULL;
    
    if (argc==2) strcpy(server_ip,argv[1]);		// gets the server hostname from argv if specified
    else strcpy(server_ip,"wiznick.ddns.net");  // else default server ip is wiznick.ddns.net
    
    char *timestamp = get_time();
    printf(GRN"%s Welcome to WChat v0.86"RESET"\n",timestamp);
    free(timestamp);

    gtk_init (&argc, &argv);

    /* create a new window */
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request (GTK_WIDGET (window), 900, 600);
    gtk_window_set_title (GTK_WINDOW (window), "WChat v0.86");
    //gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon (GTK_WINDOW(window), create_pixbuf("icon.png"));
    g_signal_connect (window, "destroy", G_CALLBACK (quit_program), NULL);
    g_signal_connect_swapped (window, "delete-event",
                              G_CALLBACK (gtk_widget_destroy), window);
    
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    gtk_widget_set_margin_top(grid,5);
    gtk_widget_set_margin_bottom(grid,1);
    gtk_widget_set_margin_start(grid,5);
    gtk_widget_set_margin_end(grid,5);
    gtk_container_add (GTK_CONTAINER (window), grid);
        
    menubar = gtk_menu_bar_new();
    gtk_widget_set_hexpand(menubar,TRUE);    
    menufile = gtk_menu_item_new_with_mnemonic("_File");
    menuhelp = gtk_menu_item_new_with_mnemonic("_Help");    
    filesubmenu = gtk_menu_new();
    helpsubmenu = gtk_menu_new();    
    item_connect = gtk_menu_item_new_with_mnemonic("_Connect");
    //item_connect = gtk_image_menu_item_new_from_stock(GTK_STOCK_CONNECT, NULL);
    item_disconnect = gtk_menu_item_new_with_mnemonic("_Disconnect");  
    //item_disconnect = gtk_image_menu_item_new_from_stock(GTK_STOCK_DISCONNECT, NULL);
    item_quit = gtk_menu_item_new_with_mnemonic("_Quit"); 
    //item_quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    item_help = gtk_menu_item_new_with_mnemonic("_Help"); 
    //item_help = gtk_image_menu_item_new_from_stock(GTK_STOCK_HELP, NULL);
    item_about = gtk_menu_item_new_with_mnemonic("_About"); 
    //item_about = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT, NULL);  
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menufile), filesubmenu);   
    gtk_menu_shell_append(GTK_MENU_SHELL(filesubmenu), item_connect);
    gtk_menu_shell_append(GTK_MENU_SHELL(filesubmenu), item_disconnect);
    gtk_menu_shell_append(GTK_MENU_SHELL(filesubmenu), item_quit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menufile);    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuhelp), helpsubmenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpsubmenu), item_help);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpsubmenu), item_about);  
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuhelp);    
    gtk_grid_attach(GTK_GRID (grid), menubar, 0, 0, 2, 1);
    g_signal_connect_swapped (item_connect, "activate", G_CALLBACK (connect_dialog), NULL);    
	g_signal_connect_swapped (item_quit, "activate", G_CALLBACK (quit_program), NULL);
	g_signal_connect_swapped (item_help, "activate", G_CALLBACK (help_window), NULL);
	g_signal_connect_swapped (item_about, "activate", G_CALLBACK (about_window), NULL);
	 
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_margin_top(box,5);
    gtk_widget_set_margin_bottom(box,5);
	
	cr_room = gtk_entry_new();
	gtk_widget_set_vexpand(cr_room,FALSE);
	gtk_widget_set_hexpand(cr_room,FALSE);
	gtk_entry_set_max_length(GTK_ENTRY(cr_room),30);
	g_signal_connect(cr_room, "activate", G_CALLBACK(create_room_request), cr_room);
	gtk_entry_set_placeholder_text (GTK_ENTRY(cr_room), "Type room name...");
	gtk_box_pack_start (GTK_BOX (box), cr_room, FALSE, FALSE, 0);
	create_btn = gtk_button_new_with_label("Create room");
	g_signal_connect_swapped (create_btn, "clicked", G_CALLBACK (create_room_request), cr_room);
	gtk_box_pack_start (GTK_BOX (box), create_btn, FALSE, FALSE, 0);
	combobox = gtk_combo_box_text_new_with_entry();	
	gtk_widget_set_margin_start(combobox,30);
	gtk_box_pack_start (GTK_BOX (box), combobox, FALSE, FALSE, 0);
	join_btn = gtk_button_new_with_label("Join room");
	g_signal_connect_swapped (join_btn, "clicked", G_CALLBACK (join_room), NULL);
	gtk_box_pack_start (GTK_BOX (box), join_btn, FALSE, FALSE, 0);	
	gtk_grid_attach(GTK_GRID (grid), box, 0, 1, 2, 1);
	roomlist = create_roomlist();
	gtk_widget_set_vexpand(roomlist,TRUE);
	gtk_widget_set_hexpand(roomlist,FALSE);
	gtk_widget_set_size_request(roomlist,160,-1);
	gtk_widget_set_margin_end(roomlist,3);
	gtk_grid_attach(GTK_GRID (grid), roomlist, 0, 2, 1, 2);
 
    vpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(vpaned,TRUE);
    gtk_widget_set_hexpand(vpaned,TRUE);
	gtk_grid_attach(GTK_GRID (grid), vpaned, 1, 2, 1, 1);

    text = create_text();
    gtk_paned_pack1 (GTK_PANED (vpaned), text,1,1);
    userlist = create_userlist();
    gtk_paned_pack2 (GTK_PANED (vpaned), userlist,0,0);

    gtk_paned_set_position(GTK_PANED (vpaned),590);

    entry = gtk_entry_new();
    gtk_widget_set_vexpand(entry,FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(entry), 150);
    g_signal_connect (entry, "activate", G_CALLBACK (enter_callback), entry);
    gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "Type your message here...");
    gtk_grid_attach(GTK_GRID (grid), entry, 1, 3, 1, 1);
       
    statusbar = gtk_statusbar_new();
    gtk_widget_set_hexpand(statusbar,TRUE);
    gchar *status = g_strdup_printf("Welcome to WChat v0.86. Connected to %s as %s.",server_ip,username);
    gtk_statusbar_push(GTK_STATUSBAR (statusbar), 0, status);
    gtk_grid_attach_next_to(GTK_GRID (grid), statusbar, roomlist, GTK_POS_BOTTOM, 2, 1);
    gtk_widget_grab_focus (entry);    
    
    gtk_widget_show_all(window);
    connect_dialog();
    
	gtk_main(); 
    
    pthread_join(snd,NULL);  
    pthread_join(rcv,NULL);    
    close(srv_sock);
	
    return 0;
}

void *listenforincoming(void *s) {	
	char* buf = calloc(BUF_SIZE,sizeof(char));
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));		

	while (read(srv_sock,buf,BUF_SIZE)) {
		//write(1,buf,BUF_SIZE);	// debug msg
		char *timestamp = get_time();
		PROTOCOL* protocol=parse_message(buf);
		//printf("Received msg of type %s\n",protocol->type); //debug msg
		if (strcmp(protocol->type,"public")==0) {
			gchar *msg = g_strdup_printf("%s %s #%s: %s",timestamp,protocol->source,protocol->target,protocol->content);
			insert_text(buffer,msg);
			free(msg);
		}
		else if (strcmp(protocol->type,"changename")==0) {			// forces a namechange 			
			strcpy(username,protocol->target);
			gchar *msg = g_strdup_printf("%s %s",timestamp,protocol->content);
			insert_text(buffer,msg);
			gchar *status = g_strdup_printf("Welcome to WChat v0.86. Connected to %s as %s.",server_ip,username);
			gtk_statusbar_push(GTK_STATUSBAR (statusbar), 0, status);
			free(msg);			
		}		
		else if (strcmp(protocol->type,"welcome")==0) {				// print the incoming welcome message
			gchar *msg = g_strdup_printf("%s %s",timestamp,protocol->content);
			insert_text(buffer,msg);
			request_room_list();				
			gchar *status = g_strdup_printf("Welcome to WChat v0.86. Connected to %s as %s.",server_ip,username);sleep(1);
			gtk_statusbar_push(GTK_STATUSBAR (statusbar), 0, status);
			request_user_list();					
			free(msg);												
		}
		else if (strcmp(protocol->type,"srvmsg")==0) {				// handle general server messages
			gchar *msg = g_strdup_printf("%s %s",timestamp,protocol->content);
			insert_text(buffer,msg);
			request_user_list();
			free(msg);
		}
		else if (strcmp(protocol->type,"private")==0) {				// print incoming private messages
			gchar *msg = g_strdup_printf("%s %s whispers to you: %s",timestamp,protocol->source,protocol->content);
			insert_text(buffer,msg);
			free(msg);
		}
		else if (strcmp(protocol->type,"user_on")==0) {				// print user on messages
			gchar *msg = g_strdup_printf("%s User %s has logged in.",timestamp,protocol->source);
			insert_text(buffer,msg);	
			request_user_list();
			free(msg);		
		}
		else if (strcmp(protocol->type,"user_off")==0) {			// print user off messages
			if (strcmp(protocol->content,"")==0) strcpy(protocol->content,"Leaving");
			gchar *msg = g_strdup_printf("%s User %s has logged off (%s).",timestamp,protocol->source,protocol->content);	
			insert_text(buffer,msg);
			request_user_list();
			free(msg);		
		}
		else if (strcmp(protocol->type,"userlist")==0) {			
			//printf("userlist received\n");		// debug msg
			update_user_list(protocol->content);			
		}
		else if (strcmp(protocol->type,"roomlist")==0) {			
			//printf("roomlist received\n");		// debug msg
			update_room_list(protocol->content);
		}
		else if (strcmp(protocol->type,"whoisonline")==0) {			// print whoisonline requests
			gchar *msg = g_strdup_printf("%s %s",timestamp,protocol->content);
			insert_text(buffer,msg);
			free(msg);						
		}
		else if (strcmp(protocol->type,"kick")==0) {		// if kicked from server, close socket and exit client
			gchar *msg = g_strdup_printf("%s %s",timestamp,protocol->content);
			insert_text(buffer,msg);			
			printf(GRN"%s You got kicked from the server. Restarting client."RESET"\n",timestamp);
			gtk_widget_destroy(window);
			close(srv_sock);
			free(timestamp); free(msg);							
			execv(args[0],args);			
		}
		memset(buf,0,BUF_SIZE);
		free(protocol);	free(timestamp);
	}
	if (!read(srv_sock,buf,BUF_SIZE)) {				// restart client if server disconnects
		char *timestamp = get_time();
		printf(RED"%s Disconnected from the server, restarting client."RESET"\n",timestamp);
		close(srv_sock);
		free(timestamp);				
		execv(args[0],args);
	}
	return 0;
}

void *sendmessages(void *s) {	
	char* msg = calloc(BUF_SIZE,sizeof(char)); 
	
	while(fgets(msg,BUF_SIZE,stdin) > 0) {
		sendusermsg(msg);
		memset(msg,0,BUF_SIZE);
	} 
	return 0;
}

void whoisonline() {
	char *timestamp = get_time();
	char *js = calloc(BUF_SIZE,sizeof(char));
	// make a json string of whoisonline type
	sprintf(js,"{\"source\":\"%s\", \"target\":\"null\", \"type\":\"whoisonline\", \"content\":\"null\", \"timestamp\":\"%s\"}",username,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	free(timestamp); free(js);
}

void update_user_list(char* userslist) {
	int len = strlen(userslist);
	int k=0,l=0;
	memset(users,0,3000);
	for (int i=10;i<len;i++) {		
		if (userslist[i]==',') {
			users[k][l]='\0';
			k++;					
			l=0;
			continue;
		}
		else {
			users[k][l]=userslist[i];
			l++;
		}
	}
	gtk_list_store_clear(GTK_LIST_STORE(umodel));
	for (int i = 0; i < 10; i++) {
		gchar *ustr = g_strdup_printf("%s", users[i]);		
		gtk_list_store_append (GTK_LIST_STORE(umodel), &useriter);
		gtk_list_store_set (GTK_LIST_STORE(umodel), &useriter, 0, ustr, -1);
		g_free (ustr);
	}
}

void request_user_list() {
	char *timestamp = get_time();
	char *js = calloc(BUF_SIZE,sizeof(char));
	sprintf(js,"{\"source\":\"%s\", \"target\":\"null\", \"type\":\"userlist\", \"content\":\"null\", \"timestamp\":\"%s\"}",username,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	free(timestamp); free(js);
}

void update_room_list(char *roomslist) {
	int len = strlen(roomslist);
	char str[3];
	int k=0,l=0;
	memset(rooms,0,1500);	
	str[0]=roomslist[6];
	str[1]=roomslist[7];
	numofrooms = atoi(str);
	for (int i=10;i<len;i++) {		
		if (roomslist[i]==',') {
			rooms[k][l]='\0';
			k++;					
			l=0;
			continue;
		}
		else {
			rooms[k][l]=roomslist[i];
			l++;
		}
	}
	gtk_list_store_clear(GTK_LIST_STORE (rmodel));
	gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(combobox));    
    for (int i = 0; i < numofrooms; i++) { 
		gchar *rstr = g_strdup_printf ("%s", rooms[i]);
        gtk_list_store_append (GTK_LIST_STORE (rmodel), &roomiter);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combobox), NULL, rstr);
        gtk_list_store_set (GTK_LIST_STORE (rmodel), &roomiter, 0, rstr, -1);
        g_free (rstr);
    }
}

void request_room_list() {
	char *timestamp = get_time();
	char *js = calloc(BUF_SIZE,sizeof(char));
	sprintf(js,"{\"source\":\"%s\", \"target\":\"Server\", \"type\":\"roomlist\", \"content\":\"null\", \"timestamp\":\"%s\"}",username,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	free(timestamp); free(js);
}

void create_room_request() {
	const gchar *roomname = gtk_entry_get_text(GTK_ENTRY(cr_room));
	create_room(roomname);
	gtk_entry_set_text(GTK_ENTRY(cr_room),"");
}

void create_room(const gchar *roomname) {
	if (strcmp(roomname,"\0")==0) return;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	char *timestamp = get_time();
	char *js = calloc(BUF_SIZE,sizeof(char));
	if (strcmp(jroom,"\0")!=0) {
		gchar *msg = g_strdup_printf("%s You are already in another room.",timestamp);
		insert_text(buffer,msg);
		free(timestamp);
		return;
	}	
	sprintf(js,"{\"source\":\"%s\", \"target\":\"Server\", \"type\":\"create\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,roomname,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	strcpy(jroom,roomname);
	free(timestamp); free(js);
	request_room_list();
}

void join_room(const gchar *roomname) {
	char *timestamp = get_time();
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	if (roomname==NULL) roomname = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox));
	if (strcmp(jroom,"\0")!=0) {
		gchar *msg = g_strdup_printf("%s You are already in another room.",timestamp);
		insert_text(buffer,msg);
		free(timestamp);
		return;
	}
		
	char *js = calloc(BUF_SIZE,sizeof(char));	
	sprintf(js,"{\"source\":\"%s\", \"target\":\"Server\", \"type\":\"join\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,roomname,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	strcpy(jroom,roomname);
	free(timestamp); free(js);		
}

void leave_room(const gchar *roomname) {
	char *timestamp = get_time();
	char *js = calloc(BUF_SIZE,sizeof(char));	
	sprintf(js,"{\"source\":\"%s\", \"target\":\"Server\", \"type\":\"leave\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,roomname,timestamp);
	write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
	strcpy(jroom,"\0");
	free(timestamp); free(js);	
}

void quit_program() {
	char *js = calloc(BUF_SIZE,sizeof(char));
	char *timestamp = get_time();
	sprintf(js,"{\"source\":\"%s\", \"target\":\"everyone\", \"type\":\"user_off\", \"content\":\"Leaving\", \"timestamp\":\"%s\"}",username,timestamp);
	write(srv_sock, js, strlen(js)+1);		// send a json string of type user_off to the server
	gtk_widget_destroy(window);
	printf(RED"%s Exiting WChat..."RESET"\n",get_time());
	exit(0);
}

void help_window() {
	gchar *txt;
	//GtkWidget *help = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	GtkWidget *help = gtk_dialog_new_with_buttons("WChat help",GTK_WINDOW(window),flags,NULL,NULL);
		
	gtk_widget_set_size_request (GTK_WIDGET (help), 600, 270);
   // gtk_window_set_title (GTK_WINDOW (help), "Wchat v0.86 - help");
   // gtk_window_set_resizable(GTK_WINDOW(help),FALSE);
   // gtk_window_set_modal(GTK_WINDOW(help),FALSE);
   // gtk_window_set_transient_for(GTK_WINDOW(help),GTK_WINDOW(window));
    
   // GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
   // gtk_container_add(GTK_CONTAINER(help),hbox);
    GtkWidget *hbox = gtk_dialog_get_content_area(GTK_DIALOG(help));
    
    GtkWidget *label = gtk_label_new("WChat v0.86 - Available commands: ");
    gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_CENTER);
    gtk_container_add(GTK_CONTAINER(hbox),label);
    
    GtkWidget *text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_widget_set_margin_start(text,5);
    gtk_widget_set_margin_end(text,5);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    txt = g_strdup_printf("/name <new_username> - to change your nickname");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/whoisonline - to see the online users");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/msg <username> <message_here> - to send a private message to an online user");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/create <room_name> - to create a new room");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/join <room_name> - to join an existing room");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/leave <room_name> - to leave a joined room");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/ban <room_name> <user_name> - to ban a user from a room (Moderator only)");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/showrooms - to see the existing rooms");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/showusers <room_name> - to see the users of a room");
    insert_text(buffer,txt);
    txt = g_strdup_printf("/help - to see the available commands (...this window)");
    insert_text(buffer,txt);    
    gtk_container_add(GTK_CONTAINER(hbox),text);
        
	GtkWidget *button = gtk_button_new_with_label("CLOSE");
	gtk_widget_set_margin_top(button,5);
	gtk_widget_set_margin_bottom(button,10);
	gtk_widget_set_margin_start(button,250);
	gtk_widget_set_margin_end(button,250);
	gtk_box_pack_start (GTK_BOX(hbox), button, FALSE, FALSE, 0);

	gtk_widget_show_all(help);
	g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_widget_destroy), help); 
}


void about_window() {
	GtkWidget *about = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request (GTK_WIDGET (about), 260, 150);
    gtk_window_set_title (GTK_WINDOW (about), "About WChat v0.86");
    gtk_window_set_icon (GTK_WINDOW(window), create_pixbuf("icon.png"));
    gtk_window_set_resizable(GTK_WINDOW(about),FALSE);
    gtk_window_set_modal(GTK_WINDOW(about),TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(about),GTK_WINDOW(window));
    
    GtkWidget *abox = gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
    gtk_container_add(GTK_CONTAINER(about),abox);
    
    GtkWidget *label = gtk_label_new("WChat v0.86\nA chat client for Linux\nby Nikolaos Perris #36261\nand Alvaro Magalhaes #37000\nProject for Operating Systems\nUniversidade Fernando Pessoa\nCopyright (c) 2019");
    gtk_label_set_justify(GTK_LABEL(label),GTK_JUSTIFY_CENTER);
    gtk_container_add(GTK_CONTAINER(abox),label);
    
	GtkWidget *button = gtk_button_new_with_label("OK");
	gtk_widget_set_margin_top(button,5);
	gtk_widget_set_margin_bottom(button,10);
	gtk_widget_set_margin_start(button,100);
	gtk_widget_set_margin_end(button,100);
	gtk_box_pack_start (GTK_BOX(abox), button, FALSE, FALSE, 0);

	gtk_widget_show_all(about);
	g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_widget_destroy), about); 
}

void sendusermsg(const gchar *gmsg) {
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	char* msg = calloc(BUF_SIZE,sizeof(char));  
	strcpy(msg,gmsg);
	int len = strlen(msg);
	for (int i=0;i<len;i++) {		// loop to replace char " with a white space, cause it was causing crashes
		if (msg[i]=='"' || msg[i]=='\\') msg[i]=' ';
	}			
	msg[strcspn(msg,"\n")]=0;		// remove the '\n' from the user input			
	if (strncmp(msg,"/",1)==0) {	// if first char is "/", check if it's a correct command
		if (strncmp(msg,"/quit",5)==0) {	// handle the /quit command
			char *tmp = calloc (BUF_SIZE,sizeof(char));
			strcpy(tmp,msg+6);			// set the correct beginning of quit message	
			char *js = calloc(BUF_SIZE,sizeof(char));
			char *timestamp = get_time();
			sprintf(js,"{\"source\":\"%s\", \"target\":\"everyone\", \"type\":\"user_off\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,tmp,timestamp);
			write(srv_sock, js, strlen(js)+1);		// send a json string of type user_off to the server
			free(timestamp); free(msg); free(js); free(tmp);
			close(srv_sock);						// close the socket
			exit(0);						
		}
		else if (strncmp(msg,"/msg",4)==0) {	// handle the private message option, using /msg command
			int j=0;
			char *target = calloc(30,sizeof(char));
			for (int i=5;i<len;i++) {		// get the target's username from the /msg targetname message_here 
				if (msg[i]==' ') {			// cycle stops when it finds the white space char
					target[j]='\0';
					break;
				}
				else {						// copy the target's name into the target variable
					target[j]=msg[i];
					j++;
				}
			}
			char *timestamp = get_time();
			char *tmp1=strlwr(username);
			char *tmp2=strlwr(target);
			if (strcmp(tmp1,tmp2)==0) {		// checks if you try to whisper ...yourself
				char *str = g_strdup_printf("%s You cannot whisper yourself.",timestamp);
				insert_text(buffer,str);
				free(str);
			}
			else {
				char *tmp = calloc (BUF_SIZE,sizeof(char));
				strcpy(tmp,msg+j+6);	// set the correct beginning of message	
				gchar *str = g_strdup_printf("%s You whisper to user %s: %s",timestamp,target,tmp);
				insert_text(buffer,str);
				char *js = calloc(BUF_SIZE,sizeof(char));
				// make a json string of private type
				sprintf(js,"{\"source\":\"%s\", \"target\":\"%s\", \"type\":\"private\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,target,tmp,timestamp);
				write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
				free(js); free(tmp); free(str);
			}
			free(timestamp); free(tmp1); free(tmp2); free(target);
		}
		else if (strcmp(msg,"/whoisonline")==0) {		// handle the /whoisonline command
			whoisonline();
		}
		else if (strncmp(msg,"/name",5)==0) {		// handle the /name new_username command
			int j=0;
			char tmp[30];
			strcpy(tmp,username);					// store previous username in tmp 
			memset(username,0,sizeof(username));
			for (int i=6;i<len;i++) {
				if (msg[i]=='\n' || msg[i]=='\0') {		// cycle stops when it finds the new line char
					username[j]='\0';
					break;
				}
				else {							// copy the new username into the username variable
					username[j]=msg[i];
					j++;
				}
			}
			int m=0;
			while (username[m]!='\0') {			// replace non-alphanumeric chars with underscore
				if (isalnum(username[m])==0) username[m]='_';
				m++;
			} 
			char *timestamp = get_time();
			if (strcmp(tmp,username)==0) {
				char *str = g_strdup_printf("%s Your new nickname is same as the old.",timestamp);
				insert_text(buffer,str);
				free(str);
			}
			else {
				char *js = calloc(BUF_SIZE,sizeof(char));
				sprintf(js,"{\"source\":\"%s\", \"target\":\"server\", \"type\":\"changename\", \"content\":\"%s\", \"timestamp\":\"%s\"}",tmp,username,timestamp);
				write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
				gchar *status = g_strdup_printf("Welcome to WChat v0.86. Connected to %s as %s.",server_ip,username);
				gtk_statusbar_push(GTK_STATUSBAR (statusbar), 0, status);
				free(js); free(status);
			}
			free(timestamp);				
		}
		else if (strncmp(msg,"/help",5)==0) {
			help_window();						
		}
		else if (strncmp(msg,"/create",7)==0) {
			char *target = calloc(30,sizeof(char));
			int j=0;
			for (int i=8;i<len;i++) {		// get the roomname from the /create roomname
				if (msg[i]=='\n' || msg[i]=='\0') {			// cycle stops when it finds the '\0' or '\n'
					target[j]='\0';
					break;
				}
				else {						// copy the roomname into the target variable
					target[j]=msg[i];
					j++;
				}
			}		
			create_room(target);
			free(target);
		}
		else if (strncmp(msg,"/join",5)==0) {
			char *target = calloc(30,sizeof(char));
			int j=0;
			for (int i=6;i<len;i++) {		// get the roomname from the /join roomname
				if (msg[i]=='\n' || msg[i]=='\0') {			// cycle stops when it finds the '\0' or '\n'
					target[j]='\0';
					break;
				}
				else {						// copy the roomname into the target variable
					target[j]=msg[i];
					j++;
				}
			}		
			join_room(target);
			free(target);
		}
		else if (strncmp(msg,"/leave",6)==0) {
			char *target = calloc(30,sizeof(char));
			int j=0;
			for (int i=7;i<len;i++) {		// get the roomname from the /leave roomname
				if (msg[i]=='\n' || msg[i]=='\0') {			// cycle stops when it finds the '\0' or '\n'
					target[j]='\0';
					break;
				}
				else {						// copy the roomname into the target variable
					target[j]=msg[i];
					j++;
				}
			}		
			leave_room(target);
			free(target);
		}	
		else if (strncmp(msg,"/showrooms",10)==0) {
			request_room_list();
		}
		else if (strncmp(msg,"/showusers",10)==0) {
			char *timestamp = get_time();
			char *js = calloc(BUF_SIZE,sizeof(char));
			char *target = calloc(30,sizeof(char));
			int j=0;
			for (int i=11;i<len;i++) {		// get the roomname from the /showusers roomname
				if (msg[i]=='\n' || msg[i]=='\0') {			// cycle stops when it finds the '\0' or '\n'
					target[j]='\0';
					break;
				}
				else {						// copy the roomname into the target variable
					target[j]=msg[i];
					j++;
				}
			}			
			sprintf(js,"{\"source\":\"%s\", \"target\":\"Server\", \"type\":\"showusers\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,target,timestamp);
			write(srv_sock, js, strlen(js)+1); 		// send the json string	to the server
			free(js); free(timestamp);
		}
		else {
			char *timestamp = get_time();
			gchar *str = g_strdup_printf("%s Invalid command: %s",timestamp,msg);
			insert_text(buffer,str);	
			free(timestamp); free(str);
		}
	}
	// make and send a json string for anything else (basically the public messages) 
	else {
		char *timestamp = get_time();
		char *js = calloc(BUF_SIZE,sizeof(char));
		sprintf(js,"{\"source\":\"%s\", \"target\":\"%s\", \"type\":\"public\", \"content\":\"%s\", \"timestamp\":\"%s\"}",username,jroom,msg,timestamp);   
		write(srv_sock, js, strlen(js)+1); 		// send the  json string	to the server
		free(js); free(timestamp);
	}
}
