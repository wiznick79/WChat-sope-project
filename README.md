# sope-project-phase2 by Nikolaos Perris (#36261) and Alvaro Magalhães (#37000)
compile with: 
gcc -o client client.c `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0` -pthread
gcc -o server server.c -pthread

server hostname can be specified as a command line (argv) parameter eg. ./client localhost
or it defaults to 'wiznick.ddns.net'
