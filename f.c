#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

// Function prototypes
void analyze_system_memory();
void analyze_process_memory();
void display_memory_mapping();
void analyze_memory_allocation();
void analyze_memory_leaks();

// Mutex for thread safety
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Global pointer to the head of the memory block list
typedef struct MemoryBlock {
    void* ptr;              // Pointer to the allocated memory block
    size_t size;            // Size of the memory block
    const char* file;       // File where allocation was made
    int line;               // Line number where allocation was made
    struct MemoryBlock* next;   // Pointer to the next memory block
} MemoryBlock;

MemoryBlock* memory_block_head = NULL;

// Function to read and display system-wide memory information
void analyze_system_memory() {
    FILE *meminfo_file;
    char line[256];
   
    // Open /proc/meminfo file
    meminfo_file = fopen("/proc/meminfo", "r");
    if (meminfo_file == NULL) {
        perror("Error opening /proc/meminfo");
        return;
    }
   
    // Read lines from /proc/meminfo
    printf("System-wide Memory Information:\n");
    while (fgets(line, sizeof(line), meminfo_file)) {
        printf("%s", line);
    }
   
    // Close file
    fclose(meminfo_file);
}

// Function to display memory usage of a process
void display_memory_usage(pid_t pid) {
    char command[50];
    sprintf(command, "pmap -x %d", pid);
    
    printf("Process-wise memory usage:\n");
    fflush(stdout);

    int status = system(command);
    if (status == -1) {
        printf("Failed to retrieve memory usage.\n");
    }
}

// Function to analyze memory of the current process
void analyze_process_memory() {
    // Allocate memory
    int *ptr = malloc(1000 * sizeof(int));
    if (ptr == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }

    // Get the process ID
    pid_t pid = getpid();

    // Print memory usage before operations
    display_memory_usage(pid);

    // Use memory (example: fill array with values)
    for (int i = 0; i < 1000; i++) {
        ptr[i] = i;
    }

    // Print memory usage after operations
    display_memory_usage(pid);

    // Free memory
    free(ptr);
}

// Function to display virtual memory mapping
void display_memory_mapping() {
    FILE *fp;
    char line[256];

    // Open the maps file for reading
    fp = fopen("/proc/self/maps", "r");
    if (fp == NULL) {
        printf("Failed to open /proc/self/maps\n");
        return;
    }

    // Read and print each line
    printf("Virtual Memory Mapping:\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        printf("%s", line);
    }

    // Close the file
    fclose(fp);
}

// Custom memory allocation wrapper
void* custom_malloc(size_t size, const char* filename, int line) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Error: Memory allocation failed in file %s at line %d\n", filename, line);
        exit(EXIT_FAILURE);
    }
    printf("Allocated %zu bytes at address %p in file %s at line %d\n", size, ptr, filename, line);
    return ptr;
}

// Custom memory deallocation wrapper
void custom_free(void* ptr, const char* filename, int line) {
    printf("Deallocated memory at address %p in file %s at line %d\n", ptr, filename, line);
    free(ptr);
}

// Process a C source file to find memory allocations and deallocations
void processFile(const char* filePath) {
    FILE* file = fopen(filePath, "r");
    if (file == NULL) {
        return;
    }

    char line[256];
    int lineNum = 0;

    while (fgets(line, sizeof(line), file)) {
        lineNum++;

        // Check for memory allocation and deallocation patterns
        if (strstr(line, "malloc")) {
            // Example: You may need to parse the line further to extract the size
            custom_malloc(10, filePath, lineNum);
        } else if (strstr(line, "free")) {
            // Example: You may need to parse the line further to extract the pointer
            custom_free(NULL, filePath, lineNum);
        }
    }

    fclose(file);
}

// Recursively process files in a directory
void processDirectory(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Ignore "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char fullPath[PATH_MAX];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

        struct stat statbuf;
        if (stat(fullPath, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                // Recursively process subdirectories
                processDirectory(fullPath);
            } else if (S_ISREG(statbuf.st_mode)) {
                // Process C source files
                if (strstr(entry->d_name, ".c")) {
                    processFile(fullPath);
                }
            }
        }
    }

    closedir(dir);
}

// Function to analyze memory allocation
void analyze_memory_allocation() {
    const char* rootDirectory = "/"; // Start analysis from the root directory
    processDirectory(rootDirectory);
}





// Function to allocate memory with tracking
void* tracked_malloc(size_t size, const char* file, int line) {
    void* ptr = malloc(size);
    if (ptr != NULL) {
        // Create a new memory block
        MemoryBlock* block = (MemoryBlock*)malloc(sizeof(MemoryBlock));
        block->ptr = ptr;
        block->size = size;
        block->file = file;
        block->line = line;
        
        // Lock mutex before accessing the global list
        pthread_mutex_lock(&mutex);
        
        // Add the new memory block to the head of the list
        block->next = memory_block_head;
        memory_block_head = block;
        
        // Unlock mutex after accessing the global list
        pthread_mutex_unlock(&mutex);
    }
    return ptr;
}

// Function to free memory with tracking
void tracked_free(void* ptr) {
    if (ptr != NULL) {
        // Lock mutex before accessing the global list
        pthread_mutex_lock(&mutex);
        
        // Find the memory block associated with the pointer
        MemoryBlock* prev = NULL;
        MemoryBlock* curr = memory_block_head;
        while (curr != NULL && curr->ptr != ptr) {
            prev = curr;
            curr = curr->next;
        }
        
        // If found, remove it from the list and free memory
        if (curr != NULL) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                memory_block_head = curr->next;
            }
            free(curr);
        }
        
        // Unlock mutex after accessing the global list
        pthread_mutex_unlock(&mutex);
        
        // Free the memory
        free(ptr);
    }
}

// Function to detect memory leaks in a specific file
void detect_memory_leaks(const char* file_name) {
    // Lock mutex before accessing the global list
    pthread_mutex_lock(&mutex);
   
    MemoryBlock* curr = memory_block_head;
    int leak_count = 0;
    while (curr != NULL) {
        if (strcmp(curr->file, file_name) == 0) {
            fprintf(stderr, "Memory leak detected: %zu bytes at %s:%d\n", curr->size, curr->file, curr->line);
            leak_count++;
        }
        curr = curr->next;
    }
   
    // Unlock mutex after accessing the global list
    pthread_mutex_unlock(&mutex);
   
    if (leak_count == 0) {
        fprintf(stderr, "No memory leaks detected in file: %s\n", file_name);
    } else {
        fprintf(stderr, "Total %d memory leak(s) detected in file: %s\n", leak_count, file_name);
    }
}


// Function to analyze memory
void analyze_memory(int choice) 

{
    char file_name[100];
    switch(choice) {
        case 1:
            analyze_system_memory();
            break;
        case 2:
            analyze_process_memory();
            break;
        case 3:
            display_memory_mapping();
            break;
        case 4:
            analyze_memory_allocation();
            break;
        case 5:
            char file_name[100];
    printf("Enter the file name to analyze memory: ");
    scanf("%s", file_name);
   
    // Allocate memory with tracking
    int* arr = (int*)tracked_malloc(10 * sizeof(int), __FILE__, __LINE__);
   
    // Use the allocated memory
    for (int i = 0; i < 10; i++) {
        arr[i] = i;
    }
   
    // Forget to free the allocated memory
    // tracked_free(arr);
   
    // Detect memory leaks in the specified file
    detect_memory_leaks(file_name);
   
        default:
            printf("Invalid choice\n");
    }
}

// Example usage
int main() {
    int choice;
    for(;;){
    printf("Enter your choice:\n");
    printf("1. Analyze System Memory\n");
    printf("2. Analyze Process Memory\n");
    printf("3. Display Memory Mapping\n");
    printf("4. Analyze Memory Allocation\n");
    printf("5. Analyze Memory Leaks\n");
    scanf("%d", &choice);
    analyze_memory(choice);
    }
    return 0;
}

