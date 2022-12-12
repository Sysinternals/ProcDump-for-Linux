#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>

#define FILE_DESC_COUNT	500
#define THREAD_COUNT	100

void* ThreadProc(void *input)
{
    sleep(UINT_MAX);
    return NULL;
};

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        //
        // To avoid timing differences, each test below should sleep indefinately once done.
        // The process will be killed by the test harness once procdump has finished monitoring
        //
        if (strcmp("sleep", argv[1]) == 0)
        {
            sleep(UINT_MAX);
        }
        else if (strcmp("burn", argv[1]) == 0)
        {
            while(1);
        }
        else if (strcmp("fc", argv[1]) == 0)
        {
          FILE* fd[FILE_DESC_COUNT];
          for(int i=0; i<FILE_DESC_COUNT; i++)
          {
              fd[i] = fopen(argv[0], "r");
          }
          memset(fd, 0, FILE_DESC_COUNT*sizeof(FILE*));
          sleep(UINT_MAX);
        }
        else if (strcmp("tc", argv[1]) == 0)
        {
          pthread_t threads[THREAD_COUNT];
          for(int i=0; i<THREAD_COUNT; i++)
          {
              pthread_create(&threads[i], NULL, ThreadProc, NULL);
          }
          sleep(UINT_MAX);
        }
    }
}