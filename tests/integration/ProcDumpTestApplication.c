#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]){
	if (argc > 1){
		if (strcmp("sleep", argv[1]) == 0){
			sleep(5);
		} else if (strcmp("burn", argv[1]) == 0){
			alarm(5);
			while(1);
		}
	}
}
