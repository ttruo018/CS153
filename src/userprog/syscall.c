#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
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

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
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
