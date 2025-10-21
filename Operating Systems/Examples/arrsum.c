#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{

  uint32_t* A = NULL;
  uint32_t* B = NULL;
  uint32_t* C = NULL;

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

  A = (uint32_t*)malloc(N*sizeof(uint32_t));
  B = (uint32_t*)malloc(N*sizeof(uint32_t));
  C = (uint32_t*)malloc(N*sizeof(uint32_t));

  for (i = 0; i < N; i++)
  {
    A[i] = i;
    C[i] = A[i] + B[i];
    printf("C[%d] = %d\n", i, C[i]);
  }
  
  free(A);
  free(B);
  free(C);  
  
  return 0;
}

