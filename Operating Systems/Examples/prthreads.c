#include <stdio.h>
#include <inttypes.h>

#include <pthread.h>

uint32_t arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

void* printArr(void *arg)
{
  uint32_t i = *((uint32_t*) arg);

  printf("(thread): arr[%d] = %d\n", i, arr[i]);
  
  pthread_exit(NULL);
}

int main(int argc, char* argv[])
{
  uint32_t i = 0;

  pthread_t threads[10] = {}; 

  for (i = 0; i < 10; i++)
  {
    pthread_create(&threads[i], NULL, printArr, (void*)&i); 
  }
  
  for (i = 0; i < 10; i++)
  {
    pthread_join(threads[i], NULL);
  }

  for (i = 0; i< 10; i++)
  {
    printf("(main): arr[%d] = %d\n", i, arr[i]);
  }
  
  return 0;
}

