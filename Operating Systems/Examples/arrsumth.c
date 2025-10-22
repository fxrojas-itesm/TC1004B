#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <pthread.h>

uint32_t* A = NULL;
uint32_t* B = NULL;
uint32_t* C = NULL;

typedef struct
{
  uint32_t i;
} thread_data;

void* vectSum(void* params)
{
  uint32_t   i = ((thread_data*)params)->i;
  A[i] = i;
  C[i] = A[i] + B[i];
  //printf("C[%d] = %d\n", i, C[i]);

  pthread_exit(NULL);
}


int main(int argc, char* argv[])
{
  pthread_t*   threads     = NULL;
  thread_data* params      = NULL;

  uint32_t N = 0;
  uint32_t i = 0;

  if (argc < 2)
  {
    printf("Program requires at least two arguments. Terminating!\n");
    return -1;
  }
  else
  {
    N = atoi(argv[1]);
  }

  A       = (uint32_t*)malloc(N*sizeof(uint32_t));
  B       = (uint32_t*)malloc(N*sizeof(uint32_t));
  C       = (uint32_t*)malloc(N*sizeof(uint32_t));
  threads = (pthread_t*)malloc(N*sizeof(pthread_t));
  params  = (thread_data*)malloc(N*sizeof(thread_data));

  for(i = 0; i < N; i++)
  {
    params[i].i = i;
    pthread_create(&threads[i], NULL, vectSum, (void*) &params[i]);
  }

  for(i = 0; i < N; i++)
  {
    pthread_join(threads[i], NULL);
    printf("C[%d] = %d\n", i, C[i]);
  }

  free(A);
  free(B);
  free(C);  
  free(threads);
  free(params);

  pthread_exit(NULL);
}

