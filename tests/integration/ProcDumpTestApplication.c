#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define FILE_DESC_COUNT	500
#define THREAD_COUNT	100
#define SLEEP_TIME 5

void* ThreadProc(void *input)
{
	sleep(SLEEP_TIME);
	return NULL;
};

int main(int argc, char *argv[])
{
	if (argc > 1)
	{
		if (strcmp("sleep", argv[1]) == 0)
		{
			sleep(SLEEP_TIME);
		}
		else if (strcmp("burn", argv[1]) == 0)
		{
			alarm(SLEEP_TIME);
			while(1);
		}
		else if (strcmp("fc", argv[1]) == 0)
		{
		  FILE* fd[FILE_DESC_COUNT];
		  for(int i=0; i<FILE_DESC_COUNT; i++)
		  {
			fd[i] = fopen(argv[0], "r");
		  }

   		  sleep(SLEEP_TIME);

		  for(int i=0; i<FILE_DESC_COUNT; i++)
		  {
			fclose(fd[i]);
		  }
		}
		else if (strcmp("tc", argv[1]) == 0)
		{
		  pthread_t threads[THREAD_COUNT];
		  for(int i=0; i<THREAD_COUNT; i++)
		  {
			pthread_create(&threads[i], NULL, ThreadProc, NULL);
		  }

		  for(int i=0; i<THREAD_COUNT; i++)
		  {
			pthread_join(threads[i], NULL);
		  }
		}
		else if (strcmp("sig", argv[1]) == 0)
		{
			sleep(SLEEP_TIME);
			raise(SIGUSR2);
			sleep(SLEEP_TIME);
		}
	}
}
