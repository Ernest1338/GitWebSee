#!/bin/bash

# MEMORY SANITIZE
# gcc -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment -lgit2 main.c -o gitwebsee.app && ./gitwebsee.app

# DEBUG BUILD
gcc -lgit2 -g -o gitwebsee.app main.c && ./gitwebsee.app

# RELEASE BUILD
# gcc -lgit2 -O3 -o gitwebsee.app main.c && ./gitwebsee.app
