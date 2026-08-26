#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<syslog.h>
#include<signal.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdbool.h>


#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024



volatile sig_atomic_t caught_sig = 0;



void signal_handler(int sig_num)
{
	if (sig_num == SIGINT || sig_num == SIGTERM)
	{
		caught_sig = 1;
	}
}


void setup_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

int daemonize(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        return -1;
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }
    
    
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        return -1;
    }
    

    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null == -1) {
        syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
        return -1;
    }
    
    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);
    close(dev_null);
    
    return 0;
}

int init_server_socket(void)
{
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
		return -1;
	}
	
	int opt =1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
	{
		syslog(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
		close(server_fd);
		return -1;
	}
	
	
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);
	
	if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))==-1)
	{
		syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
		close(server_fd);
		return -1;
	}
	
	if(listen(server_fd, 5) == -1)
	{
		syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
		close(server_fd);
		return -1;
	}
	
	return server_fd;
}



void append_data_to_file(const char *data, size_t len)
{
	int file_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if(file_fd == -1)
	{
		syslog(LOG_ERR, "Failed to open data file for writing: %s", strerror(errno));
		return;
	}
	
	
	if (write(file_fd, data, len) == -1)
	{
		syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno));
		
	}
	close(file_fd);
}


void send_file_to_client(int client_fd)
{
	int read_fd = open(DATA_FILE, O_RDONLY);
	if(read_fd == -1)
	{
		syslog(LOG_ERR, "Failed to open data file for reading: %s", strerror(errno));
		return;
	}
	
	char send_buf[BUFFER_SIZE];
	ssize_t bytes_read;
	
	while ((bytes_read = read(read_fd, send_buf, sizeof(send_buf)))> 0)
	{
		ssize_t sent = 0;
		while(sent < bytes_read)
		{
			ssize_t s = send(client_fd, send_buf + sent, bytes_read - sent, 0);
			if(s == -1)
			{	
				syslog(LOG_ERR, "Send error: %s", strerror(errno));
				break;
			}
			sent +=s;
		}
		
	}
	
	close(read_fd);
}


void handle_client(int client_fd, const char *ip_str)
{
	char recv_buf[BUFFER_SIZE];
	char *packet_buf = NULL;
	size_t packet_size = 0;
	
	syslog(LOG_INFO, "Accepted connection from %s", ip_str);
	
	while(!caught_sig)
	{
		ssize_t bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
		if(bytes_recv == 0)
		{
			break;
		}
		else if (bytes_recv == -1)
		{	
			if(errno != EINTR) syslog(LOG_ERR, "Receive error: %s", strerror(errno));
			break;
		}
		
		char *ptr = recv_buf;
		size_t remaining = bytes_recv;
		
		
		while (remaining > 0 && !caught_sig)
		{
			char *newline_pos = memchr(ptr, '\n', remaining);
			
			if(newline_pos != NULL)
			{
				size_t chunk_len = (newline_pos - ptr) + 1;
				
				char *new_buf = realloc(packet_buf, packet_size + chunk_len);
				if(new_buf == NULL)
				{
					syslog(LOG_ERR, "Memory allocation failed, discarding packet");
					free(packet_buf);
					packet_buf = NULL;
					packet_size = 0;
					break;
				}
				
				packet_buf = new_buf;
				memcpy(packet_buf + packet_size, ptr, chunk_len);
				packet_size += chunk_len;
				
				append_data_to_file(packet_buf, packet_size);
				send_file_to_client(client_fd);
				
				free(packet_buf);
				packet_buf = NULL;
				packet_size = 0;
				
				ptr += chunk_len;
				remaining -= chunk_len;
				}
				
				else
				
				{
					char *new_buf = realloc(packet_buf, packet_size + remaining);
					if(new_buf == NULL)
					{
						syslog(LOG_ERR, "Memory allocation failed, discarding packet segmentation");
						free(packet_buf);
						packet_buf = NULL;
						packet_size = 0;
						
					}
					
					else
					{
						packet_buf = new_buf;
						memcpy(packet_buf + packet_size, ptr, remaining);
						packet_size += remaining;
					}
					break;
				}
			}
		}
		
		if (packet_buf != NULL)
		{
			free(packet_buf);
		}
		close(client_fd);
		syslog(LOG_INFO, "Closed connection from %s", ip_str);
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
            syslog(LOG_ERR, "Failed to enter daemon mode");
            close(server_fd);
            closelog();
            return -1;
        }
    }

    while (!caught_sig) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR) break; 
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, sizeof(ip_str));
        
        handle_client(client_fd, ip_str);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATA_FILE);
    closelog();
    
    return 0;
}



