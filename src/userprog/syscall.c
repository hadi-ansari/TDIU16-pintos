#include <stdio.h>
#include <syscall-nr.h>
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

/* header files you probably need, they are not used yet */
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "../lib/kernel/console.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


/* This array defined the number of arguments each syscall expects.
   For example, if you want to find out the number of arguments for
   the read system call you shall write:
   
   int sys_read_arg_count = argc[ SYS_READ ];
   
   All system calls have a name such as SYS_READ defined as an enum
   type, see `lib/syscall-nr.h'. Use them instead of numbers.
 */
const int argc[] = {
  /* basic calls */
  0, 1, 1, 1, 2, 1, 1, 1, 3, 3, 2, 1, 1, 
  /* not implemented */
  2, 1,    1, 1, 2, 1, 1,
  /* extended, you may need to change the order of these two (plist, sleep) */
  0, 1
};

static void
syscall_handler (struct intr_frame *f) 
{
  int32_t* esp = (int32_t*)f->esp;
  // esp[0]  --> syscall num 
  // esp[1]  --> param
  switch ( esp[0] /* retrive syscall number */ )
  {
    case SYS_HALT:
    {
      printf ("SYSTEM HALT is running...\n");
      power_off();
      break;
    }
    
    case SYS_EXIT:
    {
      printf ("SYSTEM EXIT is running...\n");
      printf ("Stack top + 0: %d\n", esp[1]);
      thread_exit();
      break;
    }
    
    case SYS_READ:
    {
      printf ("SYSTEM READ is running...\n");

      int read_char = 0;
      char input_char;

      for (int i = 0; i < esp[3]; i++)
      {
        input_char = input_getc();
        if (input_char != -1)
        {
          if(input_char == '\r')
            input_char = '\n';
          char *buffer = esp[2];
          buffer[i] = input_char;
          read_char++;
        }
      }
      
      f->eax = read_char;
      break;
    }
    
    case SYS_WRITE:
    {
      printf ("\nSYSTEM WRITE is running...\n");
      putbuf(esp[2], esp[3]);
      break;
      
    }
    default:
    {
      printf ("Executed an unknown system call!\n");
      
      printf ("Stack top + 0: %d\n", esp[0]);
      printf ("Stack top + 1: %d\n", esp[1]);
      
      thread_exit ();
      
      break;
    }
  }
}
