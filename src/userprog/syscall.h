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
	struct semaphore sema;
	struct list_elem elem;
	enum process_status stat;
};

struct child_process * add_child_process (int pid);
struct child_process * get_child_process (int pid);
void remove_child_process (struct child_process *cp);

void filesys_free_files(struct thread *);
void syscall_init (void);
int fd_open(const char *);
int fd_read(int, void *, unsigned);
int fd_write(int, const void *, unsigned);
int fd_filesize(int );
void fd_seek(int, unsigned);
void sys_exit(int);


#endif /* userprog/syscall.h */
