[![Codacy Badge](https://api.codacy.com/project/badge/Grade/57f449c7bc2349d490d09c41a3609407)](https://app.codacy.com/gh/wiznick79/WChat-sope-project?utm_source=github.com&utm_medium=referral&utm_content=wiznick79/WChat-sope-project&utm_campaign=Badge_Grade_Settings)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/5517acd2d6ef471f8fdf080800b7ef8e)](https://www.codacy.com/gh/wiznick79/WChat-sope-project/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=wiznick79/WChat-sope-project&amp;utm_campaign=Badge_Grade)

# WChat-sope-project by Nikolaos Perris (#36261) and Alvaro Magalhães (#37000)
A chat server and client, written in C, using GKT3 for the GUI part.
It was made as a project for the class of Operating Systems, Universidade Fernando Pessoa, in 2019.

compile client with: 
gcc -o client client.c `pkg-config --cflags gtk+-3.0` `pkg-config --libs gtk+-3.0` -pthread

compile server with:
gcc -o server server.c -pthread

server hostname can be specified as a command line (argv) parameter eg. ./client localhost
or it defaults to 'wiznick.ddns.net'
