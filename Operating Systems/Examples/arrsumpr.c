#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(int argc, char* argv[])
{

  uint32_t* A = NULL;
  uint32_t* B = NULL;
  uint32_t* C = NULL;

  uint32_t N = 0;
  uint32_t i = 0;
  pid_t     pid = 0; 

  if (argc < 2)
  {
    printf("Program requires at least two arguments. Terminating!\n");
    return -1;
  }
  else
  {
    N = atoi(argv[1]);
  }

  A = mmap(NULL, N*sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  B = mmap(NULL, N*sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  C = mmap(NULL, N*sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  for (i = 0; i < N; i++)
  {
    pid = fork();
    if (pid < 0)
    {
      printf("ERROR: Process not created!");
      return -1;
    }
    else if (pid == 0) 
    {
      A[i] = i;
      C[i] = A[i] + B[i];
      
      return 0;
    }
  }

  while(wait(NULL)!=-1);

  for (i = 0; i < N; i++)
  {
    printf("C[%d] = %d\n", i, C[i]);
  }
  
  munmap(A, N*sizeof(uint32_t));
  munmap(B, N*sizeof(uint32_t));
  munmap(C, N*sizeof(uint32_t));
  
  return 0;
}
