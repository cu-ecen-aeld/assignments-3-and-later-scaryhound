#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

volatile sig_atomic_t caught_sig = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    pthread_t thread_id;
    int client_sockfd;
    bool thread_complete_flag;
} thread_data_t;

typedef struct slist_data_s {
    thread_data_t thread_param;
    SLIST_ENTRY(slist_data_s) entries;
} slist_data_t;

SLIST_HEAD(slisthead, slist_data_s) head = SLIST_HEAD_INITIALIZER(head);

void signal_handler(int sig_num) {
    if (sig_num == SIGINT || sig_num == SIGTERM) {
        caught_sig = 1;
    }
}

void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) return -1;
    if (chdir("/") < 0) return -1;
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null != -1) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
    }
    return 0;
}

int init_server_socket(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) return -1;
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        close(server_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    

    int retries = 5;
    while (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        if (retries == 0) {
            syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
            close(server_fd);
            return -1;
        }
        retries--;
        sleep(1); 
    }

    
    if (listen(server_fd, 5) == -1) {
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}


void append_data_to_file(const char *data, size_t len) {
    int file_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file_fd == -1) return;
    if (write(file_fd, data, len) == -1) {
        syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno));
    }
    close(file_fd);
}

void send_file_to_client(int client_fd) {
    int read_fd = open(DATA_FILE, O_RDONLY);
    if (read_fd == -1) return;
    char send_buf[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(read_fd, send_buf, sizeof(send_buf))) > 0) {
        ssize_t sent = 0;
        while (sent < bytes_read) {
            ssize_t s = send(client_fd, send_buf + sent, bytes_read - sent, 0);
            if (s == -1) break;
            sent += s;
        }
    }
    close(read_fd);
}


void *handle_client(void *thread_param) {
    thread_data_t *t_data = (thread_data_t *)thread_param;
    int client_fd = t_data->client_sockfd;
    char recv_buf[BUFFER_SIZE];
    char *packet_buf = NULL;
    size_t packet_size = 0;

    while (!caught_sig) {
        ssize_t bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (bytes_recv <= 0) break; // Client disconnected or error
        
        char *ptr = recv_buf;
        size_t remaining = bytes_recv;
        
        while (remaining > 0 && !caught_sig) {
            char *newline_pos = memchr(ptr, '\n', remaining);
            if (newline_pos != NULL) {
                size_t chunk_len = (newline_pos - ptr) + 1;
                char *new_buf = realloc(packet_buf, packet_size + chunk_len);
                if (new_buf == NULL) break;
                
                packet_buf = new_buf;
                memcpy(packet_buf + packet_size, ptr, chunk_len);
                packet_size += chunk_len;
                
                // --- Lock Mutex for File Operations ---
                pthread_mutex_lock(&file_mutex);
                append_data_to_file(packet_buf, packet_size);
                send_file_to_client(client_fd);
                pthread_mutex_unlock(&file_mutex);

                // Reset buffer for the next packet on this same connection
                free(packet_buf);
                packet_buf = NULL;
                packet_size = 0;
                
                ptr += chunk_len;
                remaining -= chunk_len;
            } else {
                char *new_buf = realloc(packet_buf, packet_size + remaining);
                if (new_buf != NULL) {
                    packet_buf = new_buf;
                    memcpy(packet_buf + packet_size, ptr, remaining);
                    packet_size += remaining;
                }
                break;
            }
        }
    }
    
    // This is the ONLY place the thread should exit and close the socket!
    if (packet_buf != NULL) free(packet_buf);
    close(client_fd);
    t_data->thread_complete_flag = true;
    return NULL;
}

void *timestamp_thread(void *arg) {
    syslog(LOG_INFO, "Timestamp thread spawned and running.");
    while (!caught_sig) {
        for (int i = 0; i < 10; i++) {
            if (caught_sig) break;
            sleep(1);
        }
        if (caught_sig) break;

        time_t t = time(NULL);
        struct tm tm_info;
        localtime_r(&t, &tm_info); // Thread-safe time retrieval

        char time_str[100];
        strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", &tm_info);

        char final_timestamp[150];
        int len = snprintf(final_timestamp, sizeof(final_timestamp), "timestamp:%s\n", time_str);

        pthread_mutex_lock(&file_mutex);
        append_data_to_file(final_timestamp, len);
        pthread_mutex_unlock(&file_mutex);
        
        syslog(LOG_INFO, "Wrote timestamp to file: %s", time_str);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    bool run_as_daemon = false;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = true;
    }

    openlog("aesdsocket", 0, LOG_USER);
    setup_signals();

    int server_fd = init_server_socket();
    if (server_fd == -1) {
        closelog();
        return -1;
    }

    if (run_as_daemon) {
        if (daemonize() == -1) {
            close(server_fd);
            closelog();
            return -1;
        }
    }

    // --- Start Timer Thread ---
    pthread_t timer_thread;
    if (pthread_create(&timer_thread, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to start timer thread!");
    } else {
        syslog(LOG_INFO, "Successfully created timer thread.");
    }

    while (!caught_sig) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (errno == EINTR || caught_sig) break; 
            continue; 
        }

        slist_data_t *new_node = malloc(sizeof(slist_data_t));
        if (new_node == NULL) {
            close(client_fd);
            continue;
        }
        new_node->thread_param.client_sockfd = client_fd;
        new_node->thread_param.thread_complete_flag = false;

        pthread_create(&new_node->thread_param.thread_id, NULL, handle_client, &new_node->thread_param);
        SLIST_INSERT_HEAD(&head, new_node, entries);

        // Cleanup finished threads
        slist_data_t *curr = SLIST_FIRST(&head);
            while (curr != NULL) {
                // Save the next pointer BEFORE we potentially free 'curr'
                slist_data_t *next = SLIST_NEXT(curr, entries);
                
                if (curr->thread_param.thread_complete_flag) {
                    pthread_join(curr->thread_param.thread_id, NULL);
                    
                    // Safely remove 'curr' from the list using the standard Linux macro
                    SLIST_REMOVE(&head, curr, slist_data_s, entries);
                    free(curr);
                }
                
                curr = next;
            }
    } 

    // Final cleanup
    pthread_join(timer_thread, NULL);
    
    slist_data_t *curr = SLIST_FIRST(&head);
    while (curr != NULL) {
        pthread_join(curr->thread_param.thread_id, NULL);
        slist_data_t *temp = curr;
        curr = SLIST_NEXT(curr, entries);
        free(temp);
    }

    pthread_mutex_destroy(&file_mutex);
    close(server_fd);
    remove(DATA_FILE);
    closelog();
    return 0;
}
