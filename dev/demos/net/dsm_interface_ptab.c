
// Initializes the global process table.
static void initProcessTable (unsigned int length) {
	
	// Allocate and zero out process array.
	ptab.processes = (dsm_proc *)dsm_zalloc(length * sizeof(dsm_proc));

	// Update length.
	ptab.length = length;
}

// Resize the process table (at least minLength).
static void resizeProcessTable (unsigned int minLength) {
	void *new_processes;
	unsigned int new_length;

	// Compute new length.
	new_length = MAX(minLength, 2 * ptab.length);
	
	// Allocate and zero out new process array.
	new_processes = dsm_zalloc(new_length * sizeof(dsm_proc));

	// Copy over the old array.
	memcpy(new_processes, ptab.processes, ptab.length * sizeof(dsm_proc));

	// Free old array.
	free(ptab.processes);

	// Assign new array, and update the length.
	ptab.processes = new_processes;
	ptab.length = new_length;
}

// Deallocates the process table.
static void freeProcessTable (void) {
	free(ptab.processes);
}

// Registers a new file-descriptor in the process table.
static void registerProcess (int fd, int pid) {

	// Reallocate if necessary.
	if (fd >= ptab.length) {
		resizeProcessTable(fd + 1);
	}

	// Verify entry is not occupied (should not happen).
	if (ptab.processes[fd].pid != 0) {
		dsm_cpanic("registerProcess", "Current slot occupied!");
	}
	
	// Insert new entry.
	ptab.processes[fd] = (dsm_proc) {
		.fd = fd,
		.pid = pid,
		.flags = (dsm_pstate){.is_stopped = 0, .is_waiting = 0, .is_queued = 0}
	};
}


// Unregisters a given file-descriptor from the process table.
static void unregisterProcess (int fd) {

	// Verify location is accessible.
	if (fd < 0 || fd > ptab.length || ptab.processes == NULL) {
		dsm_cpanic("unregisterProcess", "Location inaccessible!");
	}

	memset(ptab.processes + fd, 0, sizeof(dsm_proc));
}

// Prints the process table.
static void showProcessTable (void) {
	int n = 0;
	printf("------------------------- Process Table -----------------------\n");
	printf("\tFD\t\tPID\t\tSTOP\t\tWAIT\t\tQUEUE\n");
	for (int i = 0; i < ptab.length; i++) {
		if (ptab.processes[i].pid == 0) {
			continue;
		}
		n++;
		dsm_proc p = ptab.processes[i];
		char s = (p.flags.is_stopped ? 'Y' : 'N');
		char w = (p.flags.is_waiting ? 'Y' : 'N');
		char q = (p.flags.is_queued ? 'Y' : 'N');
		printf("\t%d\t\t%d\t\t%c\t\t%c\t\t%c\n", i, p.pid, s, w, q);
	}
	printf(" [%d/%d slots occupied]\n", n, ptab.length);
	printf("---------------------------------------------------------------\n");
}