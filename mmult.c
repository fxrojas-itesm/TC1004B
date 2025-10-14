#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
  uint32_t* A   = NULL;
  uint32_t* B   = NULL;
  uint32_t* C   = NULL;
  
  uint32_t  N   = 0;
  uint32_t  i   = 0;
  uint32_t  j   = 0;

  if (argc < 2)
  {
    printf("ERROR: Incorrect number of arguments!\n");
    return -1;
  }
  else
  {
    N = atoi(argv[1]);
  }

  A = (uint32_t*)malloc(N*N*sizeof(uint32_t));
  B = (uint32_t*)malloc(N*N*sizeof(uint32_t));
  C = (uint32_t*)malloc(N*N*sizeof(uint32_t));

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
    //printf("A[%d] = %d\tB[%d] = %d\n", i, A[i], i, B[i]);
  }

  for(i = 0; i < N*N; i++)
  {
    for(j = 0; j < N; j++)
    {
      C[i] += A[i/N*N + j]*B[j*N + i%N];
    }
    printf("C[%d] = %d\n", i, C[i]);
  }
  
  free(A);
  free(B);
  free(C);

  return 0;
}


