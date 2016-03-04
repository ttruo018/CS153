#include "lib/kernel/hash.h"
#include "filesys/file.h"
#include "filesys/off_t.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static struct hash filesys_fdhash;
static struct lock filesys_lock;
static struct lock process_lock;
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

struct open_file
{
	struct hash_elem h_elem;
	struct list_elem l_elem;
	int fd;
	int pid;
	struct file * file;
};

static bool verify(const char *buffer)
{
	struct thread *t = thread_current();
	return is_user_vaddr(buffer) && pagedir_get_page(t->pagedir, buffer) != NULL;
}

static int allocate_fd(void)
{
	static int fd_curr = 2;
	return fd_curr++;
}

static unsigned filesys_fdhash_func (const struct hash_elem *e, void *aux)
{
	struct open_file *ret = hash_entry(e, struct open_file, h_elem);
	return (unsigned)ret->fd;
}

static bool filesys_fdhash_less(const struct hash_elem *a, const struct hash_elem *b, void * aux)
{
	struct open_file *a_file = hash_entry(a, struct open_file, h_elem);
	struct open_file *b_file = hash_entry(b, struct open_file, h_elem);
	
	return (a_file->fd < b_file->fd);
}


static struct open_file * fd_to_open_file(int fd)
{
	struct open_file s;
	s.fd = fd;
	struct thread *t = thread_current();
	struct hash_elem *f = hash_find (&filesys_fdhash, &s.h_elem);
	if(f == NULL)
	{
		return NULL;
	}
	struct open_file * ret = hash_entry(f, struct open_file, h_elem);
	if(t->tid != ret->pid)
	{
		return NULL;
	}
	return ret;
}


static void filesys_free_open_file(struct open_file * opened_file)
{
	file_close(opened_file->file);
	lock_acquire(&filesys_lock);
	hash_delete(&filesys_fdhash, &opened_file->h_elem);
	lock_release(&filesys_lock);
	list_remove(&opened_file->l_elem);
	free(opened_file);
}

void free_open_files(struct thread * t)
{
	struct list_elem * e;
	for(e = list_begin(&t->openFiles); e != list_end(&t->openFiles);)
	{
		struct list_elem * nextElement = list_next(e);
		struct open_file * file_being_freed = list_entry(e, struct open_file, l_elem);
		filesys_free_open_file(file_being_freed);
		e = nextElement;
	}
}
static struct file * fd_to_file(int fd)
{
	lock_acquire(&filesys_lock);
	struct open_file * f = fd_to_open_file(fd);
	lock_release(&filesys_lock);
	if(f == NULL)
	{
		return NULL;
	}
	return f->file;
}
static void sys_halt (void);
void sys_exit (int status);
static pid_t sys_exec (const char *cmd_line);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

struct semaphore file_acc;

void
syscall_init (void) 
{
  	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&filesys_lock);
	hash_init(&filesys_fdhash, filesys_fdhash_func, filesys_fdhash_less, NULL);
	lock_init(&process_lock);
}

/* Copies SIZE bytes from user address USRC to kernel address DST.
 * Call thread_exit() if any of the user accesses are invalid. */
static void copy_in (void *dst_, const void *usrc_, size_t size)
{
	uint8_t *dst = dst_;
	const uint8_t *usrc = usrc_;

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
	struct thread *t = thread_current();
	return (uaddr < PHYS_BASE && pagedir_get_page (t->pagedir, uaddr) != NULL);
}

static void
syscall_handler (struct intr_frame *f ) 
{
  unsigned callNum;
  int args[3];
  int numOfArgs;
  int i;
  //##Get syscall number
  if(!verify_user(f->esp))
  {
 	sys_exit(-1);
  }

  copy_in (&callNum, f->esp, sizeof callNum);

  //##Using the number find out which system call is being used
  numOfArgs = syscall_arg[callNum];

  for(i = 1; i <= numOfArgs; i++)
  {
  	if(!verify_user(f->esp + i))
  	{
 		sys_exit(-1);
 	}
  }

  copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * numOfArgs);

  //##Use switch statement or something and run this below for each
  //##Depending on the callNum...
  //f->eax = desired_syscall_fun (args[0],args[1], args[2]);
  switch(callNum)
  {
		case 0 :
			sys_halt();
			break;
		case 1 :
			sys_exit(args[0]);
			break;
		case 2 :
			f->eax = sys_exec(args[0]);
			break;
		case 3 :
			f->eax = sys_wait(args[0]);
			break;
		case 4 :
			f->eax = sys_create(args[0], args[1]);
			break;
		case 5 :
			f->eax = sys_remove(args[0]);
			break;
		case 6 :
			f->eax = sys_open(args[0]);
			break;
		case 7 :
			f->eax = sys_filesize(args[0]);
			break;
		case 8 :
			f->eax = sys_read(args[0], args[1], args[2]);
			break;
		case 9 :
			f->eax = sys_write(args[0], args[1], args[2]);
			break;
		case 10 :
			sys_seek(args[0], args[1]);
			break;
		case 11 :
			f->eax = sys_tell(args[0]);
			break;
		case 12 :
			sys_close(args[0]);
			break;
		default:
			thread_exit();
	
  }
}

static void sys_halt (void)
{
	shutdown_power_off();
}

void sys_exit (int status)
{
	//lock_acquire(&process_lock);
	/*struct thread *cur = thread_current();
	printf ("%s: exit(%d)\n", cur->name, status);
	if(thread_alive(cur->parent))
	{
		struct list_elem *e;
		for(e = list_begin( &cur-> children); e != list_end(&cur->children); e = list_next(e))
		{
			struct child_process *cp = list_entry(e, struct child_process, elem);
			thread_current()->wait->status = status;
		}
	}*/
	/*file_allow_write(&thread_current()->execFile);
	file_close(thread_current()->execFile);
	lock_release(&process_lock);*/
	thread_current()->wait->status = status;
	thread_exit();
	NOT_REACHED();
}

static pid_t sys_exec(const char * cmd_line)
{
	if(cmd_line == NULL || !verify(cmd_line))
	{
		sys_exit(-1);
	}
	char * command = copy_in_string(cmd_line);
	pid_t pid = process_execute(command);
	
	return pid == TID_ERROR ? -1 : pid;
}

static int sys_wait(pid_t pid)
{
	return process_wait(pid);
}

static bool sys_create (const char *file, unsigned initial_size)
{
	if(file == NULL || !verify(file))
	{
		sys_exit(-1);
	}
	return filesys_create(file, initial_size);
}

static bool sys_remove(const char *path)
{
	if(!verify(path))
	{
		return false;
	}
	
	bool success = false;
	struct file * file = filesys_open(path);
	if(file != NULL)
	{
		file_close(file);
		success = filesys_remove(path);
	}
	return success;
}

int fd_open(const char * file)
{
	struct file * fileOpen = filesys_open(file);
	struct thread * t = thread_current();
	if(fileOpen == NULL)
	{
		return -1;
	}
	struct open_file * hash = malloc(sizeof(struct open_file));
	if(hash == NULL)
	{
		file_close(fileOpen);
		return -1;
	}
	int new_fd = allocate_fd();
	hash->fd = new_fd;
	hash->file = fileOpen;	
	hash->pid = t->tid;
	
	lock_acquire(&filesys_lock);
	hash_insert(&filesys_fdhash, &hash->h_elem);
	lock_release(&filesys_lock);
	
	list_push_back(&t->openFiles, &hash->l_elem);
	return new_fd;
}


static int sys_open(const char *file)
{
	if(file == NULL || !verify(file)) 
	{
		sys_exit(-1);
	}
	return fd_open(file);
}

static int sys_filesize(int fd)
{
	struct file * fileOpen = fd_to_file(fd);
	if(fileOpen == NULL)
	{
		return -1;
	}
	return file_length(fileOpen);
}

int fd_read(int fd, void *buffer, unsigned size)
{
	struct file * fileOpen = fd_to_file(fd);
	if(fileOpen == NULL) 
	{
		return -1;
	}
	return file_read(fileOpen, buffer, size);
}

static int conRead(char * buffer, unsigned size)
{
	unsigned i;
	for(i = 0; i < size; i++)
	{
		*(buffer++) = input_getc();
	}
	return size;
}

static int sys_read(int fd, void * buffer, unsigned size)
{
	int totalBytes = 0;
	if(!verify_user(buffer) || !verify_user(buffer+size))
	{
		sys_exit(-1);
	}
	while(size > 0)
	{
		unsigned bytes_on_page = PGSIZE - pg_ofs (buffer);
		unsigned bytes_to_read = bytes_on_page;
		if(bytes_on_page > size)
		{
			bytes_to_read = size;
		}
		int bytes_read = 0;
		if(!verify_user(buffer))
		{
			thread_exit();	
		}
		if(fd == STDIN_FILENO)
		{
			bytes_read = conRead(buffer, size);
		}
		else
		{
			bytes_read = fd_read(fd, buffer, size);
		}
		
		totalBytes += bytes_read;
		if(bytes_read != (int)bytes_to_read)
		{
			return totalBytes;
		}
		size -= bytes_read;
		buffer += bytes_read;
	}
	return totalBytes;
}

int fd_write(int fd, const void * buffer, unsigned size)
{
	struct file * fileOpen = fd_to_file(fd);
	if(fileOpen == NULL) 
	{
		return -1;
	}
	return file_write(fileOpen, buffer, size);
}

static int console_write(char * buffer, unsigned size) // chunks of 128 bytes each
{
	unsigned size_count = size;
	while(size_count > 128) 
	{
		putbuf(buffer, 128); 
		buffer = (const char *) buffer + 128; 
		size_count -= 128; 
	}
	putbuf(buffer, size_count);
	return size;
}

static int sys_write(int fd, void * buffer, unsigned size)
{
	int total_bytes = 0;
	//lock_acquire(&filesys_lock);
	while(size > 0)
	{
		unsigned bytes_on_page = PGSIZE - pg_ofs(buffer);
		unsigned bytes_to_write = bytes_on_page;
		if(bytes_on_page > size)
		{
			bytes_to_write = size;
		}
		int bytes_written;
		
		if(!verify(buffer) || !verify_user(buffer+size))
		{
			//lock_release(&filesys_lock);
			sys_exit(-1);
		}
		
		if(fd == STDOUT_FILENO)
		{
			bytes_written = console_write(buffer, size);
		}
		else
		{
			bytes_written = fd_write(fd, buffer, size);
		}
		
		total_bytes += bytes_written;
		size -= bytes_written;
		buffer += bytes_written;

		if(bytes_written != (int) bytes_to_write)
		{
			return total_bytes;
		}	
	}
	//lock_release(&filesys_lock);
	return total_bytes;
}

void fd_seek(int fd, unsigned position)
{
	struct file * fileOpen = fd_to_file(fd);
	if(fileOpen == NULL)// || file_is_dir(fileOpen))
	{
		return;
	}
	file_seek(fileOpen, position);
}

static void sys_seek(int fd, unsigned position)
{
	fd_seek(fd, position);
}

static unsigned sys_tell (int fd)
{
	struct file * fileOpen = fd_to_file(fd);
	return file_tell(fileOpen);
} 

static void sys_close(int fd)
{
	struct open_file * elem = fd_to_open_file(fd);
	if(elem != NULL)
	{
		filesys_free_open_file(elem);
	}
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
