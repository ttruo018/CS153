#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include "filesys/off_t.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static struct hash filesys_fdhash;
static struct lock filesys_lock;
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

struct fd_elem
{
	struct hash_elem h_elem;
	struct list_elem l_elem;
	int fd;
	int owner_pid;
	struct file * file;
};

static bool verify(const void *uadder)
{
	if(uadder == NULL)
	{
		return false;
	}
	return (uadder < PHYS_BASE && pagedir_get_page(thread_current()->pagedir, uadder) != NULL);
}

static int allocate_fd(void)
{
	static int fd_curr = 2;
	return fd_curr++;
}

static unsigned filesys_fdhash_func (const struct hash_elem *e, void *aux)
{
	struct fd_elem *elem = hash_entry(e, struct fd_elem, h_elem);
	return (unsigned) &elem->fd;
}

static bool filesys_fdhash_less(const struct hash_elem *a, const struct hash_elem *b, void * aux)
{
	struct fd_elem *a_fd = hash_entry(a, struct fd_elem, h_elem);
	struct fd_elem *b_fd = hash_entry(b, struct fd_elem, h_elem);
	
	return (&a_fd->fd < &b_fd->fd);
}

static struct fd_elem * filesys_get_fd_elem(int fd)
{
	struct fd_elem s;
	s.fd = fd;
	
	struct hash_elem *f;
	f = hash_find (&filesys_fdhash, &s.h_elem);
	
	if(f == NULL)
	{
		return NULL;
	}
	struct fd_elem * fd_element = hash_entry(f, struct fd_elem, h_elem);
	
	return (thread_current()->tid == fd_element->owner_pid) ? fd_element : NULL;
}

void halt (void);
void sys_exit (int status);
pid_t exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int sys_open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

struct semaphore file_acc;

void
syscall_init (void) 
{
  	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&filesys_lock);
	hash_init(&filesys_fdhash, filesys_fdhash_func, filesys_fdhash_less, NULL);
	process_init();
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
			halt();
			break;
		case 1 :
			sys_exit(args[0]);
			break;
		case 2 :
			f->eax = exec(args[0]);
			break;
		case 3 :
			f->eax = wait(args[0]);
			break;
		case 4 :
			f->eax = create(args[0], args[1]);
			break;
		case 5 :
			f->eax = remove(args[0]);
			break;
		case 6 :
			f->eax = sys_open(args[0]);
			break;
		case 7 :
			f->eax = filesize(args[0]);
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

void sys_exit (int status)
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

pid_t exec(const char * cmd_line)
{
	if(cmd_line == NULL || !verify(cmd_line))
	{
		sys_exit(-1);
	}
	char * command = copy_in_string(cmd_line);
	pid_t pid = process_execute(command);
	
	return pid == TID_ERROR ? -1 : pid;
}

int wait(pid_t pid)
{
	return process_wait(pid);
}

bool create (const char *file, unsigned initial_size)
{
	if(file != NULL || !verify(file))
	{
		return false;
	}
	return filesys_create(file, initial_size);
}

bool remove(const char *path)
{
	if(!verify(path))
	{
		return false;
	}
	
	bool success = false;
	struct file * file = filesys_open(path);
	if(file)
	{
		file_close(file);
		success = filesys_remove(path);
	}
	return success;
}

int fd_open(const char * file)
{
	struct file * fileOpen = filesys_open(file);
	if(!fileOpen)
	{
		sys_exit(-1);
	}
	
	struct fd_elem * hash = malloc(sizeof(struct fd_elem));
	if(hash == NULL)
	{
		file_close(file);
		return -1;
	}
	
	hash->fd = allocate_fd();
	hash->file = fileOpen;	
	hash->owner_pid = thread_current()->tid;
	
	lock_acquire(&filesys_lock);
	hash_insert(&filesys_fdhash, &hash->l_elem);
	lock_release(&filesys_lock);
	
	list_push_back(&thread_current()->openFiles, &hash->l_elem);
	return hash->fd;
}

int sys_open(const char *file)
{
	if(!verify(file))
	{
		return -1;
	}
	return fd_open(file);
}

int filesize(int fd)
{
	struct file * fileOpen;
	lock_acquire(&filesys_lock);
	struct fd_elem * fileFound = filesys_get_fd_elem(fd);
	lock_release(&filesys_lock);
	if(fileFound == NULL)
	{
		return -1;
	}
	fileOpen = fileFound->file;
	return file_length(fileOpen);
}

int fd_read(int fd, void *buffer, unsigned size)
{
	struct file * fileOpen;
	lock_acquire(&filesys_lock);
	struct fd_elem * fileFound = filesys_get_fd_elem(fd);
	lock_release(&filesys_lock);
	if(fileFound == NULL) 
	{
		return -1;
	}
	off_t bytes_read = file_read(fileOpen, buffer, size);
	return bytes_read;
}

/*void sys_exit(int status)
{
	printf("%s: exit(%i)\n", thread_current()->name, status);
	thread_current()->wait->status = status;
	lock_release(&thread_current()->wait->wait_lock);
	thread_exit();
	NOT_REACHED();
}*/

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
