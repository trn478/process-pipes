#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>

/**
 * Read an unsigned int from the given file descriptor.
 * Print a message if an error occurs.
 */
unsigned int read_int(int fd) {
    unsigned int i = 0;
    int bytes_read = read(fd, &i, sizeof(unsigned int));
    if (bytes_read < 0) {
        perror("Error reading from pipe");
    }
    return i;
}

/**
 * Write an unsigned int to the given file descriptor.
 * Print an error message and return -1 if it fails.
 * Return the number of bytes written.
 */
int write_int(int fd, unsigned int i) {
    int bytes;
    if ((bytes = write(fd, &i, sizeof(unsigned int))) < 0 && errno != EPIPE) {
        // If an error occurs that's not EPIPE
        fprintf(stderr, "Process (pid %d) failed to write i == %d to pipe %d: %s\n", getpid(), i, fd, strerror(errno));
        return -1;
    }
    return bytes;
}

/**
 * Close the given file descriptor, or print an error message if it fails.
 */
void close_or_print_error(int fd) {
    if (close(fd) < 0) {
        fprintf(stderr, "Process (pid %d) failed to close pipe %d: %s\n", getpid(), fd, strerror(errno));
    }
}

/**
 * Process a range of values by creating a child process and sending (k + 1)..n to the child.
 * 
 * Number to be identified as an RSA number is n.
 * The value of current process is m.
 * The value of m in child process is m_next.
 * File descriptor to read values from parent process is fd_in.
 * 
 * We want to return the number of filters used in this function.
 *
 */
int process_range(unsigned int n, unsigned int m, unsigned int m_next, unsigned int factor, int fd_in) {
    // Create a pipe
    int status;
    int fd[2];
    if (pipe(fd) < 0)
        perror("pipe");
    
    // Create the child process
    int child_pid = fork();
    if (child_pid > 0) { // parent
        // Close the 'reading' end of the pipe
        close_or_print_error(fd[0]);
        if (fd_in != -1) {
            // This is a child of another process
            // Read values from the parent and send only values that are not multiples of m
            unsigned int i;
            while ((i = read_int(fd_in))) {
                if (i % m != 0) {
                    // i is not a multiple of m
                    // Send i to the new child
                    if (write_int(fd[1], i) == -1)
                        break;
                }
            }
            // Close the 'reading' end of the pipe from the parent
            close_or_print_error(fd_in);
        } else {
            // This is the root process
            // Send each value in the range (m_next + 1)..ceil(n / 2) to the child
            unsigned int i;
            for (i = m_next + 1; i <= n; i++) {
                // Send i to the child
                if (write_int(fd[1], i) == -1)
                    break;
            }
        }
        
        // Close the 'writing' end of the pipe
        close_or_print_error(fd[1]);
        
        // Wait for the child to exit
        if (waitpid(child_pid, &status, 0) != -1) {
            if (WIFEXITED(status)) {
                if (fd_in != -1) {
                    // This is a child of another process
                    // exit with (child status) + 1
                    exit(WEXITSTATUS(status) + 1);
                } else {
                    // This is the root process
                    return WEXITSTATUS(status);
                }
            }
        } else {
            fprintf(stderr, "waitpid: error in parent (pid %d) waiting for child (pid %d): %d: %s\n", getpid(), child_pid, errno, strerror(errno));
            exit(1);
        }
    } else if (child_pid == 0) { // child
        // Close the 'writing' end of the pipe
        close_or_print_error(fd[1]);
        // Close the 'reading' end of the old pipe from the grandparent
        if (fd_in != -1)
            close_or_print_error(fd_in);
        
        // If m is a factor of n...
        if (n % m_next == 0) {
            if (factor) {
                // If the first factor has already been found...
                if (factor * m_next == n) {
                    // If m is the second factor, print and exit
                    printf("%d %d %d\n", n, factor, m_next);
                    close_or_print_error(fd[0]);
                    exit(1);
                } else {
                    // m not the second factor, so n must have more than 2 prime factors
                    printf("%d is not the product of two primes\n", n);
                    close_or_print_error(fd[0]);
                    exit(1);
                }
            } else if (m_next * m_next == n) {
                // If m_next^2 == n, print and exit
                printf("%d %d %d\n", n, m_next, m_next);
                close_or_print_error(fd[0]);
                exit(1);
            } else if (n == m_next) {
                // If m_next == n, n is prime
                printf("%d is prime\n", n);
                close_or_print_error(fd[0]);
                exit(1);
            } else {
                // Otherwise, set factor to m
                factor = m_next;
            }
        }
        
        // Find the first value in the list of values that is not a multiple of m,
        // and create a new process if it is greater than sqrt(n)
        unsigned int factor2;
        double n_sqrt = sqrt(n);
        while ((factor2 = read_int(fd[0]))) {
            if (factor2 % m_next != 0) {
                // i is not a multiple of m
                if (factor2 < n_sqrt) {
                    // Send i to the new child as the next value of m
                    process_range(n, m_next, factor2, factor, fd[0]);
                    fprintf(stderr, "Error: Child (pid %d) returned from process_range(%d, %d, %d, %d, %d)\n", getpid(), n, m_next, factor2, factor, fd[0]);
                    exit(1);
                } else if (factor) { // i >= sqrt(n) and first factor has been found
                    // Loop through the rest of the numbers to find the second prime factor
                    bool factor_found = false;
                    do {
                        if (factor2 % m_next != 0 && factor * factor2 == n) {
                            factor_found = true;
                            printf("%d %d %d\n", n, factor, factor2);
                            break;
                        }
                    } while ((factor2 = read_int(fd[0])));
                    if (!factor_found) {
                        printf("%d is not the product of two primes\n", n);
                    }
                } else if (factor2 * factor2 == n) {
                    // If i^2 == n, print and exit
                    printf("%d %d %d\n", n, factor2, factor2);
                } else {
                    // Otherwise, n is prime
                    printf("%d is prime\n", n);
                }
                // Close the 'reading' end of the pipe from the parent
                close_or_print_error(fd[0]);
                exit(1);
            }
        }
    } else { // error
        perror("fork");
        exit(1);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage:\n\tpfact n\n");
        exit(1);
    }
    // Get the integer value of the argument
    long n = strtol(argv[1], NULL, 0);
    if (errno == ERANGE) {
        perror("strtol");
        exit(1);
    }
    if (n <= 1 || n > UINT_MAX) {
        fprintf(stderr, "Usage:\n\tpfact n\n");
        exit(1);
    }
    
    // Ignore SIGPIPE signals
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(1);
    }
    
    printf("Number of filters = %d\n", process_range((unsigned int) n, -1, 2, 0, -1));
    
    return 0;
}