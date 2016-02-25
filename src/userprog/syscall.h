#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "threads/thread.h"

typedef int pid_t;

struct child_process
{
	int pid;
	int load;
	bool wait;
	bool exit;
	int status;
	struct lock wait_lock;
	struct list_elem elem;
	enum process_status stat;
};

struct child_process * add_child_process (int pid);
struct child_process * get_child_process (int pid);
void remove_child_process (struct child_process *cp);

void syscall_init (void);

#endif /* userprog/syscall.h */
