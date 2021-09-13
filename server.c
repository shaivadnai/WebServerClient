#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define ANY 1
#define FIFO 2
#define HPIC 3
#define HPHC 4

typedef struct
{
	// placeholder, I don't know what the descriptor uses to describe connections
	int occupied;
	int socketfd;
	int hit;
	char *buffer;
	char *extension;
	long request_time;
	int dispatch_count;
	long dispatch_time;

} Connection_descriptor;

typedef struct Producer_parameter
{
	struct sockaddr_in cli_addr;
	int listenfd;
} Producer_parameter;

typedef struct
{
	pthread_t thread_id;
	int html_count;
	int image_count;
} Thread_attributes;

void *consumer(void *ptr);
void *producer(void *ptr);
void put_in_buffer(Connection_descriptor descriptor);
void logger(int type, char *s1, char *s2, int socket_fd);
void pthread_exit(void *retval);
Connection_descriptor take_off_buffer();
void web(Connection_descriptor desc);
Connection_descriptor validate_web_request(int fd, int hit);
Connection_descriptor hpic();
Connection_descriptor hphc();
Connection_descriptor fifo();
long time_diff(struct timeval start, struct timeval end);
int increment_attribute(Connection_descriptor desc);
Thread_attributes *find_attribute_struct(pthread_t id);

struct
{
	char *ext;
	char *filetype;
} extensions[] = {
	{"gif", "image/gif"},
	{"jpg", "image/jpg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"ico", "image/ico"},
	{"zip", "image/zip"},
	{"gz", "image/gz"},
	{"tar", "image/tar"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{0, 0}};

// Create buffer
Connection_descriptor *buffer;
int buffer_counter = 0;
Connection_descriptor index0;
Connection_descriptor index1;
Connection_descriptor index2;

Thread_attributes *attributes_array;

//pthread exit
void *retval;

// create mutex and condition variable
pthread_mutex_t the_mutex;
pthread_cond_t condc, condp;

//
int global_listenfd;
int server_scheduling;
int thread_pool_size;
int buffer_size;
int did_hit_capacity;

// statistics variables
struct timeval server_start;
int dispatch_count;
pthread_mutex_t completed_mutex;
int completed_count;

//logger result
bool discard_request;

void *producer(void *ptr) /* produce data */
{

	int hit, socketfd, status;
	socklen_t length;

	did_hit_capacity = 0;

	struct Producer_parameter *p;
	p = (struct Producer_parameter *)ptr;
	printf("%s\n", "Entered Producer");

	//pthread_mutex_lock(&the_mutex);
	pthread_t thread_pool[thread_pool_size];
	attributes_array = malloc(thread_pool_size * sizeof(Thread_attributes));
	for (int i = 0; i < thread_pool_size; i++)
	{
		status = pthread_create(&thread_pool[i], NULL, consumer, (void *)p);
		Thread_attributes attr;
		attr.thread_id = thread_pool[i];
		attr.html_count = 0;
		attr.image_count = 0;
		attributes_array[i] = attr;
		/*Tells the kernel that these threads are not going to rejoin, when they die, their resources can be collected*/
		pthread_detach(thread_pool[i]);
		if (status != 0)
		{
			printf("Error code %d\n", status);
			exit(-1);
		}
	}
	for (hit = 1;; hit++)
	{
		length = sizeof(p->cli_addr);

		// wait and accept incoming socket connection
		if ((socketfd = accept(p->listenfd, (struct sockaddr *)&p->cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);

		struct timeval current_time;
		gettimeofday(&current_time, NULL);
		long request_time = time_diff(server_start, current_time);

		Connection_descriptor descriptor = validate_web_request(socketfd, hit);
		if (discard_request == true)
		{
			discard_request = false;
			continue;
		}

		descriptor.request_time = request_time;
		pthread_mutex_lock(&the_mutex); /* get exclusive access to buffer */
		int local_buffer_counter = buffer_counter;
		while (local_buffer_counter == buffer_size)
		{
			pthread_cond_wait(&condp, &the_mutex);
		}

		put_in_buffer(descriptor);
		pthread_mutex_unlock(&the_mutex);					   /* release access to buffer */
		pthread_cond_broadcast(&condc); /* wake up consumer */ //TODO check
	}
	pthread_exit(0);
}
void *consumer(void *ptr) /* consume data */
{
	//int i;
	//for (i = 1; i <= MAX; i++)
	while (true)
	{
		pthread_mutex_lock(&the_mutex); /* get exclusive access to buffer */
		while (buffer_counter == 0)
		{
			pthread_cond_wait(&condc, &the_mutex);
		}

		Connection_descriptor desc = take_off_buffer();

		pthread_cond_signal(&condp);	  /* wake up producer */
		pthread_mutex_unlock(&the_mutex); /* release access to buffer */
		web(desc);
		discard_request = false;
	}
	pthread_exit(0);
}

Connection_descriptor take_off_buffer()
{
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];
	Connection_descriptor null;
	null.occupied = 0;

	struct timeval current_time;

	if (server_scheduling == ANY)
	{
		null = fifo();
		null.dispatch_count = dispatch_count;
		dispatch_count++;

		gettimeofday(&current_time, NULL);
		null.dispatch_time = time_diff(server_start, current_time);

		increment_attribute(null);
		return null;
	}
	else if (server_scheduling == FIFO)
	{
		null = fifo();
		null.dispatch_count = dispatch_count;
		dispatch_count++;

		gettimeofday(&current_time, NULL);
		null.dispatch_time = time_diff(server_start, current_time);

		increment_attribute(null);
		return null;
	}
	else if (server_scheduling == HPIC)
	{
		null = hpic();
		null.dispatch_count = dispatch_count;
		dispatch_count++;

		gettimeofday(&current_time, NULL);
		null.dispatch_time = time_diff(server_start, current_time);

		increment_attribute(null);
		return null;
	}
	else if (server_scheduling == HPHC)
	{
		null = hphc();
		null.dispatch_count = dispatch_count;
		dispatch_count++;

		gettimeofday(&current_time, NULL);
		null.dispatch_time = time_diff(server_start, current_time);

		increment_attribute(null);
		return null;
	}
	return null;
}

int increment_attribute(Connection_descriptor desc)
{
	Thread_attributes *ta;
	ta = find_attribute_struct(pthread_self());
	if (ta->thread_id == 0)
	{
		return 1;
	}

	if (*desc.extension == 't')
	{
		ta->html_count++;
		return 0;
	}
	else if (*desc.extension == 'i')
	{
		ta->image_count++;
		return 0;
	}
	else
	{
		return 1;
	}
}

Thread_attributes *find_attribute_struct(pthread_t id)
{
	for (int i = 0; i < thread_pool_size; i++)
	{
		if (attributes_array[i].thread_id == id)
		{
			return &attributes_array[i];
		}
	}
	return NULL;
}

Connection_descriptor fifo()
{
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];

	int minimum_index;
	minimum_index = -1;

	for (int i = 0; i < buffer_size; i++)
	{
		if (buffer[i].occupied != 0)
		{
			if (minimum_index == -1)
			{
				minimum_index = i;
				continue;
			}
			if (buffer[minimum_index].hit > buffer[i].hit)
			{
				minimum_index = i;
			}
		}
	}
	buffer_counter--;
	Connection_descriptor desc = buffer[minimum_index];
	buffer[minimum_index].occupied = 0;
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];
	return desc;
}

Connection_descriptor hpic()
{
	/*HPIC
		returns FIRST image request
		if no image exists, defaults to FIFO*/
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];

	int minimum_index;
	minimum_index = -1;

	for (int i = 0; i < buffer_size; i++)
	{
		if (buffer[i].occupied != 0 && *buffer[i].extension == 'i')
		{

			if (minimum_index == -1)
			{
				minimum_index = i;
				continue;
			}
			if (buffer[minimum_index].hit > buffer[i].hit)
			{
				minimum_index = i;
			}
		}
	}

	if (minimum_index == -1)
	{
		return fifo();
	}

	buffer_counter--;
	Connection_descriptor desc = buffer[minimum_index];
	buffer[minimum_index].occupied = 0;
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];
	return desc;
}

Connection_descriptor hphc()
{
	/*HPHC
		returns FIRST text request
		if no image exists, defaults to FIFO */
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];

	int minimum_index;
	minimum_index = -1;

	for (int i = 0; i < buffer_size; i++)
	{
		if (buffer[i].occupied != 0 && *buffer[i].extension == 't')
		{

			if (minimum_index == -1)
			{
				minimum_index = i;
				continue;
			}
			if (buffer[minimum_index].hit > buffer[i].hit)
			{
				minimum_index = i;
			}
		}
	}

	if (minimum_index == -1)
	{
		return fifo();
	}

	buffer_counter--;
	Connection_descriptor desc = buffer[minimum_index];
	buffer[minimum_index].occupied = 0;
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];
	return desc;
}

void put_in_buffer(Connection_descriptor descriptor)
{
	index0 = buffer[0];
	index1 = buffer[1];
	index2 = buffer[2];
	descriptor.occupied = 1;
	for (int i = 0; i < buffer_size; i++)
	{
		if (buffer[i].occupied == 0)
		{
			buffer[i] = descriptor;
			buffer_counter++;
			index0 = buffer[0];
			index1 = buffer[1];
			index2 = buffer[2];
			return;
		}
	}
}

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE * 2];

	ssize_t a = -1;
	switch (type)
	{
	case ERROR:
		(void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
		break;
	case FORBIDDEN:
		/*(void)*/ a = write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271);
		(void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
		break;
	case NOTFOUND:
		/*(void)*/ a = write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224);
		(void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
		break;
	case LOG:
		(void)sprintf(logbuffer, " INFO: %s:%s:%d", s1, s2, socket_fd);
		break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if ((fd = open("nweb.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		/*(void)*/ a = write(fd, logbuffer, strlen(logbuffer));
		/*(void)*/ a = write(fd, "\n", 1);
		(void)close(fd);
	}
	if (type == ERROR)
	{
		/*When an error occurs, we want the whole program to go down, just the thread that
		was processing the request. So we call pthread exit to kill the thread Must unloock and signal
		manually.*/
		exit(3);
	}
	if (type == NOTFOUND || type == FORBIDDEN)
	{
		/*When a NOTFOUND or FORBIDDEN error occurs, we don't want the whole program to go down, just want to discard the offending request*/
		discard_request = true;
	}
	if (a != -1)
		sleep(1);
}

Connection_descriptor validate_web_request(int fd, int hit)
{
	int j, buflen;
	long i, ret, len;
	char *fstr;
	static char request_contents[BUFSIZE + 1]; /* static so zero filled */

	Connection_descriptor null;

	ret = read(fd, request_contents, BUFSIZE); /* read Web request in one go */
	if (ret == 0 || ret == -1)
	{ /* read failure stop now */
		logger(FORBIDDEN, "failed to read browser request", "", fd);
		if (discard_request == true)
		{
			return null;
		}
	}
	if (ret > 0 && ret < BUFSIZE)  /* return code is valid chars */
		request_contents[ret] = 0; /* terminate the buffer */
	else
		request_contents[0] = 0;
	for (i = 0; i < ret; i++) /* remove CF and LF characters */
		if (request_contents[i] == '\r' || request_contents[i] == '\n')
			request_contents[i] = '*';
	logger(LOG, "request", request_contents, hit);
	if (strncmp(request_contents, "GET ", 4) && strncmp(request_contents, "get ", 4))
	{
		logger(FORBIDDEN, "Only simple GET operation supported", request_contents, fd);
		if (discard_request == true)
		{
			return null;
		}
	}
	for (i = 4; i < BUFSIZE; i++)
	{ /* null terminate after the second space to ignore extra stuff */
		if (request_contents[i] == ' ')
		{ /* string is "GET URL " +lots of other stuff */
			request_contents[i] = 0;
			break;
		}
	}
	for (j = 0; j < i - 1; j++) /* check for illegal parent directory use .. */
		if (request_contents[j] == '.' && request_contents[j + 1] == '.')
		{
			logger(FORBIDDEN, "Parent directory (..) path names not supported", request_contents, fd);
			if (discard_request == true)
			{
				return null;
			}
		}
	if (!strncmp(&request_contents[0], "GET /\0", 6) || !strncmp(&request_contents[0], "get /\0", 6)) /* convert no filename to index file */
		(void)strcpy(request_contents, "GET /index.html");

	/* work out the file type and check we support it */
	buflen = strlen(request_contents);
	fstr = (char *)0;
	for (i = 0; extensions[i].ext != 0; i++)
	{
		len = strlen(extensions[i].ext);
		if (!strncmp(&request_contents[buflen - len], extensions[i].ext, len))
		{
			fstr = extensions[i].filetype;
			break;
		}
	}
	if (fstr == 0)
	{
		logger(FORBIDDEN, "file extension type not supported", request_contents, fd);
		if (discard_request == true)
		{
			return null;
		}
	}
	Connection_descriptor desc;
	desc.buffer = malloc(sizeof(char) * buflen);
	strcpy(desc.buffer, request_contents);
	desc.hit = hit;
	desc.extension = fstr;
	desc.occupied = 1;
	desc.socketfd = fd;
	return desc;
}

void web(Connection_descriptor desc)
{
	int a, file_fd, fd, hit, local_complete_count, status;
	long len, ret;
	char *fstr;
	char *buffer; //static so zero filled
	buffer = malloc(sizeof(char) * 8096);
	strcpy(buffer, desc.buffer);
	fstr = desc.extension;
	fd = desc.socketfd;
	hit = desc.hit;

	if ((file_fd = open(&buffer[5], O_RDONLY)) == -1)
	{ /* open the file for reading */
		logger(NOTFOUND, "failed to open file", &buffer[5], fd);
		if (discard_request == true)
		{
			return;
		}
	}
	logger(LOG, "SEND", &buffer[5], hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	(void)lseek(file_fd, (off_t)0, SEEK_SET);

	struct timeval current_time;
	gettimeofday(&current_time, NULL);
	long completed_time;
	completed_time = time_diff(server_start, current_time);

	/* lseek back to the file start ready for reading */
	(void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	logger(LOG, "Header", buffer, hit);
	/*(void)*/ a = write(fd, buffer, strlen(buffer));

	/* Send the statistical headers described in the paper, example below
    
    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	(void)write(fd,buffer,strlen(buffer));
    */

	pthread_mutex_lock(&completed_mutex);
	local_complete_count = ++completed_count;

	//Based on the definiton of "completed" as given on Piazza, it's prudent to begin writing the response in a critical region.

	(void)sprintf(buffer, "X-stat-req-arrival-count: %d\r\n", hit - 1);
	status = write(fd, buffer, strlen(buffer));
	pthread_mutex_unlock(&completed_mutex);

	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-req-arrival-time: %ld\r\n", desc.request_time);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}
	(void)sprintf(buffer, "X-stat-req-dispatch-count: %d\r\n", desc.dispatch_count);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-req-dispatch-time: %ld\r\n", desc.dispatch_time);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-req-complete-count: %d\r\n", local_complete_count - 1);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}
	(void)sprintf(buffer, "X-stat-req-complete-time: %ld\r\n", completed_time);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	/*not getting the right value*/
	(void)sprintf(buffer, "X-stat-req-age: %d\r\n", hit - desc.dispatch_count - 1);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	Thread_attributes attr;
	attr = *find_attribute_struct(pthread_self());

	(void)sprintf(buffer, "X-stat-thread-id: %ld\r\n", attr.thread_id);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-thread-count: %d\r\n", attr.html_count + attr.image_count);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-thread-html: %d\r\n", attr.html_count);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	(void)sprintf(buffer, "X-stat-thread-image: %d\r\n", attr.image_count);
	status = write(fd, buffer, strlen(buffer));
	if (status == -1)
	{
		logger(ERROR, "couldn't write to socket", "socket", fd);
	}

	/* send file in 8KB block - last block may be smaller */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		a = write(fd, buffer, ret);
	}
	if (a != -1)
		sleep(1);
	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	//exit(1); removed because its not a child process anymore
}

long time_diff(struct timeval start, struct timeval end)
{
	long start_ms, end_ms, diff;

	start_ms = (long)start.tv_usec / 1000 + (long)start.tv_sec * 1000;
	end_ms = (long)end.tv_usec / 1000 + (long)end.tv_sec * 1000;
	diff = end_ms - start_ms;
	return diff;
}

int main(int argc, char **argv)
{
	int i, port, listenfd; //, pid, listenfd, socketfd, hit;
	//socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	if (argc < 6 || argc > 6 || !strcmp(argv[1], "-?"))
	{

		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
					 "\tnweb is a small and very safe mini web server\n"
					 "\tnweb only servers out file/web pages with extensions named below\n"
					 "\t and only from the named directory or its sub-directories.\n"
					 "\tThere is no fancy features = safe and secure.\n\n"
					 "\tExample: nweb 8181 /home/nwebdir &\n\n"
					 "\tOnly Supports:",
					 VERSION);
		for (i = 0; extensions[i].ext != 0; i++)
			(void)printf(" %s", extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
					 "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
					 "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
		exit(0);
	}
	if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
		!strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
		!strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
		!strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
	{
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
		exit(3);
	}
	if (chdir(argv[2]) == -1)
	{
		(void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
		exit(4);
	}
	/* Become deamon + unstopable and no zombies children (= no wait()) */
	if (fork() != 0)
		return 0;					/* parent returns OK to shell */
	(void)signal(SIGCHLD, SIG_IGN); /* ignore child death */
	(void)signal(SIGHUP, SIG_IGN);	/* ignore terminal hangups */
	for (i = 0; i < 32; i++)
		(void)close(i); /* close open files */
	(void)setpgrp();	/* break away from process group */
	logger(LOG, "nweb starting", argv[1], getpid());
	/* setup the network socket */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		logger(ERROR, "system call", "socket", 0);
	port = atoi(argv[1]);
	if (port < 0 || port > 60000)
		logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		logger(ERROR, "system call", "bind", 0);
	if (listen(listenfd, 64) < 0)
		logger(ERROR, "system call", "listen", 0);

	/*Setting threadpool size, must be greater than 0*/

	thread_pool_size = atoi(argv[3]);
	if (thread_pool_size < 1)
		logger(ERROR, "invalid thread pool size", "arg", 0);

	/*Setting buffer size, must be greater than 0*/
	buffer_size = atoi(argv[4]);
	if (buffer_size < 1)
		logger(ERROR, "invalid buffer size", "arg", 0);

	/*Setting scheduling alg*/
	if (!strcmp(argv[5], "ANY"))
	{
		server_scheduling = 1;
	}
	else if (!strcmp(argv[5], "FIFO"))
	{
		server_scheduling = 2;
	}
	else if (!strcmp(argv[5], "HPIC"))
	{
		server_scheduling = 3;
	}
	else if (!strcmp(argv[5], "HPHC"))
	{
		server_scheduling = 4;
	}
	else
	{
		logger(ERROR, "invalid scheduling algorithm", "arg", 0);
	}

	// Create master thread? Or is this process already considered the master thread?
	// The master thread is what should create these worker threads and the thread pool
	buffer = (Connection_descriptor *)malloc(buffer_size * sizeof(Connection_descriptor));
	for (int i = 0; i < buffer_size; i++)
	{
		Connection_descriptor cd;
		cd.occupied = 0;
		buffer[i] = cd;
	}

	int status = gettimeofday(&server_start, NULL);
	if (status == -1)
	{
		logger(ERROR, "server time couldn't be checked", "arg", 0);
	}

	dispatch_count = 0;
	completed_count = 0;
	discard_request = false;

	pthread_t master;
	pthread_mutex_init(&the_mutex, 0);
	pthread_mutex_init(&completed_mutex, 0);
	pthread_cond_init(&condc, 0);
	pthread_cond_init(&condp, 0);

	struct Producer_parameter par;
	par.cli_addr = cli_addr;
	par.listenfd = listenfd;
	global_listenfd = listenfd;

	void *p = &par;
	status = pthread_create(&master, 0, producer, p);
	pthread_join(master, 0);
	printf("%d", status);
	if (status != 0)
	{
		printf("Error code %d\n", status);
		exit(-1);
	}
	pthread_cond_destroy(&condc);
	pthread_cond_destroy(&condp);
	pthread_mutex_destroy(&the_mutex);
}
