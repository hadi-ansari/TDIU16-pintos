/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>
#include <stdio.h>

int
main(void)
{
  //halt();
  exit(21);
  /* not reached */
  printf("dfasdfdsafdsafdsafasddfdsa");
  return 0;
}
