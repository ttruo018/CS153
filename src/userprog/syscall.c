#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static inline bool get_user (uint8_t *dst, const uint8_t *usrc);
static uint8_t syscall_arg[] = 
{
	0, /*Halt*/
	1, /*Exit*/
	1, /*Exec*/
	1, /*Wait*/
	2, /*Create*/
	1, /*Remove*/
	1, /*Open*/
	1, /*Filesize*/
	3, /*Read*/
	3, /*Write*/
	2, /*Seek*/
	1, /*Tell*/
	1, /*Close*/
};
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Copies SIZE bytes from user address USRC to kernel address DST.
 * Call thread_exit() if any of the user accesses are invalid. */
static void copy_in (void *dst_, const void *usrc_, size_t size)
{
	uint8_t *dst = dst_;
	const uint8_t *usrc = usrc;

	for (; size > 0; size--, dst++, usrc++)
	{
		if(usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc))
		{
			thread_exit();
		}
	}
}

/* Creates a copy of user string US in kernel memory and returns it as
 * a page that must be freed with palloc_free_page(). Truncates the string at
 * PGSIZE bytes in size. Call thread_exit() if any of the user accesses are invalid. */
static char * copy_in_string (const char *us)
{
	char *ks;
	size_t length;

	ks = palloc_get_page (0);
	if(ks == NULL)
	{
		thread_exit();
	}
	for( length = 0; length < PGSIZE; length++)
	{
		if(us >= (char *) PHYS_BASE || !get_user (ks + length, us++))
		{
			palloc_free_page (ks);
			thread_exit();
		}
		if(ks[length] == '\0')
		{
			return ks;
		}
	}
	ks[PGSIZE-1] = '\0';
	return ks;
}

/* Copies a byte from user address USRC to kernel address DST. USRC must be 
 * below PHYS_BASE. Returns true if successful, false if a segfault occured. */
static inline bool get_user (uint8_t *dst, const uint8_t *usrc)
{
	int eax;
	asm("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
		: "=m" (*dst), "=&a" (eax) : "m" (*usrc));
	return eax != 0;
}

/* Returns true if UADDR is a valid, mapped user address, false otherwise. */
static bool verify_user (const void *uaddr)
{
	return (uaddr < PHYS_BASE 
			&& pagedir_get_page (thread_current()->pagedir, uaddr) != NULL);
}

static void
syscall_handler (struct intr_frame *f ) 
{
  /*printf ("system call!\n");
  thread_exit ();*/
  unsigned callNum;
  int args[3];
  int numOfArgs;

  //##Get syscall number
  copy_in (&callNum, f->esp, sizeof callNum);

  //##Using the number find out which system call is being used
  numOfArgs = syscall_arg[callNum];

  copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * numOfArgs);

  //##Use switch statement or something and run this below for each
  //##Depending on the callNum...
  //f->eax = desired_syscall_fun (args[0],args[1], args[2]);
  switch(callNum)
  {
		case 0 :
			f->eax = halt();
			break;
		case 1 :
			f->eax = exit(args[0]);
			break;
		case 2 :
			//f->eax = exec();
			break;
		case 3 :
			//f->eax = wait();
			break;
		case 4 :
			//f->eax = create();
			break;
		case 5 :
			//f->eax = remove();
			break;
		case 6 :
			//f->eax = open();
			break;
		case 7 :
			//f->eax = filesize();
			break;
		case 8 :
			//f->eax = read();
			break;
		case 9 :
			//f->eax = write();
			break;
		case 10 :
			//f->eax = seek();
			break;
		case 11 :
			//f->eax = tell();
			break;
		case 12 :
			//f->eax = close();
			break;
	
  }
}

void halt (void)
{
	shutdown_power_off();
}

void exit (int status)
{
	struct thread *cur = thread_current();
	if(thread_alive(cur->parent))
	{
		struct list_elem *e;
		for(e = list_begin( &cur-> children); e != list_end(&cur->children); e = list_next(e))
		{
			struct child_process *cp = list_entry(e, struct child_process, elem);
			cp->status = status;
		}
	}
	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}


struct child_process * add_child_process ( int pid) 
{
	struct child_process *cp = malloc(sizeof( struct child_process) );
	cp->pid = pid;
	cp->load = 0;
	cp->wait = false;
	cp->exit = false;
	lock_init(&cp->wait_lock);
	list_push_back(&thread_current()->children, &cp->elem);

	return cp;
}

struct child_process * get_child_process ( int pid) 
{
	struct thread *t = thread_current();
	struct list_elem *e;

	for(e = list_begin(&t->children); e!= list_end(&t->children); e = list_next(e))
	{
		struct child_process *cp = list_entry(e, struct child_process, elem);
		if(cp->pid == pid) {
			return cp;
		}
	}
}

void remove_child_process (struct child_process *cp)
{
	list_remove(&cp->elem);
	free(cp);
}
