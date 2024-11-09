#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>

#define FILE_DESC_COUNT	500
#define THREAD_COUNT	100


void* dFunc(int type)
{
        if(type == 0)
        {
                char* alloc = malloc(10000);
                for(int i=0; i<10000; i++)
                {
                        alloc[i] = 'a';
                }
                mlock(alloc, 10000);
                return alloc;
        }
        else if (type == 1)
        {
                char* callocAlloc = calloc(1, 10000);
                mlock(callocAlloc, 10000);
                return callocAlloc;
        }
        else if (type == 2)
        {
                void* lastAlloc = malloc(10000);
                void* newAlloc = realloc(lastAlloc, 20000);
                for(int i=0; i<20000; i++)
                {
                        ((char*)newAlloc)[i] = 'a';
                }                
                mlock(newAlloc, 20000);
                return newAlloc;
        }
        else if (type == 3)
        {
#ifdef __linux__                
                void* lastAlloc = malloc(10000);
                void* newAlloc = reallocarray(lastAlloc, 10, 20000);
                return newAlloc;
#endif                
                return NULL;
        }
        else
        {
                return NULL;
        }
}

void* c(int type)
{
        return dFunc(type);
}

void* b(int type)
{
        return c(type);
}

void* a(int type)
{
        return b(type);
}

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
        else if (strcmp("mem", argv[1]) == 0)
        {
          sleep(10);
          for(int i=0; i<1000; i++)
          {
            a(0);
            a(1);
            a(2);
            a(3);
          }

          sleep(UINT_MAX);
        }
    }
}