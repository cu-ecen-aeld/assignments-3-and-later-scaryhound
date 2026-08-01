#include<stdio.h>
#include<syslog.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<unistd.h>
#include<errno.h>

void print_usage(const char *prog_name)
{
	fprintf(stderr, "Error: Invalid number of arguments.\n");
	fprintf(stderr, "Usage: %s <writefile><writestf>\n", prog_name);

}

int write_to_file(const char *filepath, const char *content)
{
	int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd==-1) 
	{
		syslog(LOG_ERR, "Error: could not open or create file '%s'. Reason:%s", filepath, strerror(errno));
		fprintf(stderr, "Error: could not create or write to file '%s'. \n", filepath);
		return -1;
	}
	
	size_t len = strlen(content);
	ssize_t bytes_written = write(fd, content, len);
	
	if (bytes_written == -1)
	{
		syslog(LOG_ERR, "Error: Failed to write data to file '%s'. Reason: %s", filepath, strerror(errno));
        fprintf(stderr, "Error: Could not create or write to file '%s'.\n", filepath);
        close(fd);
        return -1;
	}
	
	if (write(fd, "\n", 1) == -1)
	{
		syslog(LOG_ERR, "Error: Failed to write newline to file '%s'. Reason: %s", filepath, strerror(errno));
       		fprintf(stderr, "Error: Could not create or write to file '%s'.\n", filepath);
        	close(fd);
        	return -1;
        }
        
        close(fd);
    	return 0;
}

int main(int argc, char *argv[]) 
{
    
    openlog("writer", 0, LOG_USER);

    if (argc != 3) 
    {
        syslog(LOG_ERR, "Invalid number of arguments. Expected 2, got %d.", argc - 1);
        print_usage(argv[0]);
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    if (write_to_file(writefile, writestr) != 0) 
    {
        closelog();
        return 1; 
    }

    closelog();
    return 0; 
}

