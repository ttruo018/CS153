#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

struct child_process
{
	int pid;
	int load;
	bool wait;
	bool exit;
	int status;
	struct lock wait_lock;
	struct list_elem elem;
};

struct child_process * add_child_process (int pid);
struct child_process * get_child_process (int pid);
void remove_child_process (struct child_process *cp);

void syscall_init (void);

#endif /* userprog/syscall.h */
