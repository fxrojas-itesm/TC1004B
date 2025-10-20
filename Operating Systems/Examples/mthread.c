#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <pthread.h>

uint32_t* A             = NULL;
uint32_t* B             = NULL;
uint32_t* C             = NULL; 

uint32_t  N             = 0;

typedef struct
{
  uint32_t i;
} thread_data;

void* dotp(void* params)
{
  uint32_t   i = ((thread_data*)params)->i;
  uint32_t   j = 0;

  //pthread_detach(pthread_self());

  for(j = 0; j < N; j++)
  {
    C[i] += A[i/N*N + j]*B[j*N + i%N];
  }

  //printf("C[%d] = %d\n", i, C[i]);

  pthread_exit(NULL);
}

int main(int argc, char* argv[])
{
  uint32_t   i            = 0;

  pthread_t*   threads     = NULL;
  thread_data* params      = NULL;

  if (argc < 2)
  {
    printf("ERROR: Incorrect number of arguments!\n");
    return -1;
  }
  else
  {
    N = atoi(argv[1]);
  }

  A           = (uint32_t*)malloc(N*N*sizeof(uint32_t));
  B           = (uint32_t*)malloc(N*N*sizeof(uint32_t));
  C           = (uint32_t*)malloc(N*N*sizeof(uint32_t));
  threads     = (pthread_t*)malloc(N*N*sizeof(pthread_t));
  params      = (thread_data*)malloc(N*N*sizeof(thread_data));

  for (i = 0; i < N*N; i++)
  {
    if (i/N == i%N)
    {
      A[i] = 1;
    }
    else
    {
      A[1] = 0;
    }

    B[i] = i;
  }

  for(i = 0; i < N*N; i++)
  {
    params[i].i = i;
    pthread_create(&threads[i], NULL, dotp, (void*) &params[i]);
  }

  for(i = 0; i < N*N; i++)
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

