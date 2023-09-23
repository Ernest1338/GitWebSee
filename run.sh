#!/bin/bash

# gcc -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment -lgit2 main.c -o gitwebsee.app && ./gitwebsee.app

gcc -lgit2 main.c -o gitwebsee.app && ./gitwebsee.app
