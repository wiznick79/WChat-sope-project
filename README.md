# sope-project-phase2 by Nikolaos Perris (#36261) and Alvaro Magalhães (#37000)
A chat server and client, written in C, using GKT3 for the GUI part.
It was made as a project for the class of Operating Systems, Universidade Fernando Pessoa, in 2019.

compile client with: 
gcc -o client client.c `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0` -pthread

compile server with:
gcc -o server server.c -pthread

server hostname can be specified as a command line (argv) parameter eg. ./client localhost
or it defaults to 'wiznick.ddns.net'
