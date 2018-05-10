#include "dsm_manager.h"

/*
 *******************************************************************************
 *                              Internal Symbols                               *
 *******************************************************************************
*/

// [DELETE AFTER] The default nulled fblock structure.
#define DSM_DEFAULT_FBLOCK		(dsm_fblock){0, 0, NULL}


/*
 *******************************************************************************
 *                              Type Definitions                               *
 *******************************************************************************
*/


// [DELETE AFTER] Data structure describing a free heap block.
typedef struct dsm_fblock {
	size_t 		size;
	size_t 		offset;
	struct 		dsm_fblock *next;
} dsm_fblock;

// Provides all bookkeeping information for shared object. Lives at object base.
typedef struct dsm_table {
	dsm_fblock	freelist;			// Linked-list of free heap blocks.
	size_t 		stack_offset;		// The stack pointer offset.
	size_t 		break_offset;		// The heap break offset.
	size_t		object_size;		// The size of the shared object.
	int			users;				// The number of page users.
	sem_t 		sem_access;			// The access semaphore.
} dsm_table;


/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/


// The mapped shared object.
void *shared_map;

// The shared object table pointer. Lives in the mapped object.
dsm_table *table;


/*
 *******************************************************************************
 *                        Private Function Definitions                         *
 *******************************************************************************
*/

// [DELETE AFTER] Exits with an error message from src.
void panic (const char *src, const char *msg) {
	fprintf(stderr, "Fatal [%d]: \"%s\". Reason: \"%s\"\n", getpid(), src, msg);
	exit(EXIT_FAILURE);
}

// Attempts to safely decrement a semaphore. Exits fatally on error.
static void down (sem_t *s) {
	if (sem_wait(s) == -1) {
		panic("Failed to decrement semaphore!", strerror(errno));
	}
}

// Attempts to safely increment a semaphore. Exits fatally on error.
static void up (sem_t *s) {
	if (sem_post(s) == -1) {
		panic("Failed to increment semaphore!", strerror(errno));
	}
}

// Locks table access and adds n to user count.
static void add_users (int n) {
	down(&(table->sem_access));
	table->users += n;
	up(&(table->sem_access));
}

// [DELETE AFTER] Prints the table to stdout.
void showTable (dsm_table *tp) {
	dsm_fblock fb = tp->freelist;
	printf("------------- Table -------------\n");
	printf(".freelist = {%zu, %zu, %p}\n", fb.size, fb.offset, fb.next);
	printf(".stack_offset = %zu\n", tp->stack_offset);
	printf(".break_offset = %zu\n", tp->break_offset);
	printf(".object_size = %zu\n", tp->object_size);
	printf(".users = %d\n", tp->users);
	printf(".sem_access = ?\n");
	printf("---------------------------------\n\n");
}

// Initializes a new table of given size at supplied pointer. Returns pointer.
dsm_table *init_table (size_t size, dsm_table *tp) {

	// Setup table to default starting values.
	*tp = (dsm_table) {
		.freelist 		= DSM_DEFAULT_FBLOCK,	// Default starting block.
		.stack_offset 	= size,					// Stack grows downwards.
		.break_offset	= sizeof(dsm_table),	// The break is after table.
		.object_size	= size,					// The size of shared object.
		.users			= 1,					// This process is only user.
		.sem_access		= (sem_t){}				// Uninitialized semaphore.
	};

	// Initialize semaphore as shared and open.
	if (sem_init(&(tp->sem_access), 1, 1) == -1) {
		panic("Couldn't init access semaphore!", strerror(errno));
	}

	return tp;
}

// Maps the given file of specified size into memory. Exits fatally on error.
static void *mapSharedObject (int fd, size_t size) {
	void *map;

	// Attempt to map shared object into process memory as.
	if ((map = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0))
		== MAP_FAILED) {
		panic("Couldn't map object into memory!", strerror(errno));
	}

	return map;
}
 

/*
 *******************************************************************************
 *                         Public Function Definitions                         *
 *******************************************************************************
*/


// Initializes the shared memory object and table. Exits fatally on error.
void dsm_init (const char *name) {
	int is_new = 0, fd = 0;
	size_t size;
	struct stat sb;
	const char *n = (name == NULL ? DSM_DEFAULT_OBJ_NAME : name);

	// Attempt to create an exclusive object first. Ignore error if doesn't exist.
	if ((fd = shm_open(n, O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR)) != -1) {
		printf("[%d] Created the shared object!\n", getpid());
		is_new = 1;
	}

	// Attempt to create shared object if it didn't exist. Set new flag.
	if (fd == -1) {
		printf("[%d] Opening the shared object!\n", getpid());
		is_new = 1;
		if ((fd = shm_open(n,O_RDWR, S_IRUSR|S_IWUSR)) == -1) {
			panic("Could not open shared object!", strerror(errno));
		}
	}

	// Obtain object size. 
	if (fstat(fd, &sb) == -1) {
		panic("fstat failed!", strerror(errno));
	} else {
		size = MAX(sb.st_size, DSM_MIN_OBJ_SIZE);
	}

	// Resize object if necessary.
	if (ftruncate(fd, size) == -1) {
		panic("Failed to resize object!", strerror(errno));
	}

	// Map object into memory.
	shared_map = mapSharedObject(fd, size);

	// If new object, setup table and increment users.
	if (is_new) {
		table = init_table(size, (dsm_table *)shared_map);
		add_users(1);
	}

	// Close the file descriptor.
	close(fd); 
}

// Unmaps shared object from memory. Destroys if only owner.
void dsm_destroy (const char *name) {
	const char *n = (name == NULL ? DSM_DEFAULT_OBJ_NAME : name);
 
	// Verify arguments.
	if (table == NULL || shared_map == NULL) {
		panic("Cannot destroy NULL object!", "map or object is NULL");
	}

	// Unmap memory manually.
	if (munmap(shared_map, table->object_size) == -1) {
		panic("Couldn't unmap shared object!", strerror(errno));
	} else {
		shared_map = table = NULL;
	}

	// Destroy if only object owner.
	if (table->users <= 1) {
		printf("[%d] I'm destroying the object!\n", getpid());
		if (shm_unlink(n) == -1) {
			panic("Couldn't destroy shared object!", strerror(errno));
		}
	}
	
}

/*
 *******************************************************************************
 *                                    Main                                     *
 *******************************************************************************
*/

void child_program (void) {

	dsm_init(NULL);

	down(&(table->sem_access));
	printf("[%d] Table\n", getpid());
	showTable(table);
	up(&(table->sem_access));

	sleep(1);
	
	dsm_destroy(NULL);

	exit(EXIT_SUCCESS);
}

int main (void) {

	// Launch children.
	for (int i = 0; i < 3; i++) {
		if (fork() == 0) {
			child_program();
		}
	}

	// Wait for children to terminate.
	for (int i = 0; i < 3; i++) {
		wait(NULL);
	}

	return 0;

}

