/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100

pthread_barrier_t barrier;
sem_t fifo;
pthread_cond_t *cond_t_array;
pthread_mutex_t the_mutex;
//requester parameters

typedef struct
{
  char *host;
  char *port;
  char *file_descriptor1;
  char *file_descriptor2;
  int file_count;
  int most_recent_file;
  int clientfd;
  int thread_id;
} Requester_Parameter;

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char *host, char *port)
{
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res)))
  {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info)
{
  if (info == NULL)
    return -1;

  int clientfd;
  for (; info != NULL; info = info->ai_next)
  {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0)
    {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0)
    {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}

// Send GET request
void GET(int clientfd, char *path)
{
  char req[1000] = {0};
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
  send(clientfd, req, strlen(req), 0);
}

void *concur_requester(void *ptr)
{
  int clientfd;
  char buf[BUF_SIZE];
  Requester_Parameter param;
  param = *((Requester_Parameter *)ptr);

  char *current_file;

  while (true)
  {
    //grab the right file (alternating)

    if (param.file_count == 1)
    {
      current_file = param.file_descriptor1;
    }
    else
    {
      if (param.most_recent_file == 1)
      {
        current_file = param.file_descriptor1;
        param.most_recent_file = 2;
      }
      else
      {
        current_file = param.file_descriptor2;
        param.most_recent_file = 1;
      }
    }
    pthread_barrier_wait(&barrier);
    // Establish connection with <hostname>:<port>

    clientfd = establishConnection(getHostInfo(param.host, param.port));
    if (clientfd == -1)
    {
      fprintf(stderr,
              "[main:73] Failed to connect to: %s:%s%s \n",
              param.host, param.port, current_file);
      exit(3);
    }
    param.clientfd = clientfd;

    // Send GET request > stdout
    GET(param.clientfd, current_file);
    while (recv(param.clientfd, buf, BUF_SIZE, 0) > 0)
    {
      fputs(buf, stdout);
      memset(buf, 0, BUF_SIZE);
    }
  }
}

void *fifo_requester(void *ptr)
{
  int clientfd;
  char buf[BUF_SIZE];
  Requester_Parameter param;
  param = *((Requester_Parameter *)ptr);

  char *current_file;

  while (true)
  {
    //grab the right file (alternating)
    pthread_mutex_lock(&the_mutex); /* be the only thread running */

    if (param.file_count == 1)
    {
      current_file = param.file_descriptor1;
    }
    else
    {
      if (param.most_recent_file == 1)
      {
        current_file = param.file_descriptor1;
        param.most_recent_file = 2;
      }
      else
      {
        current_file = param.file_descriptor2;
        param.most_recent_file = 1;
      }
    }


    // Establish connection with <hostname>:<port>

    clientfd = establishConnection(getHostInfo(param.host, param.port));
    if (clientfd == -1)
    {
      fprintf(stderr,
              "[main:73] Failed to connect to: %s:%s%s \n",
              param.host, param.port, current_file);
      exit(3);
    }
    param.clientfd = clientfd;

    // Send GET request > stdout
    GET(param.clientfd, current_file);

    pthread_cond_signal(&(cond_t_array[param.thread_id + 1]));

    while (recv(param.clientfd, buf, BUF_SIZE, 0) > 0)
    {
      fputs(buf, stdout);
      memset(buf, 0, BUF_SIZE);
    }
    pthread_barrier_wait(&barrier);
    //pthread_cond_signal(&(cond_t_array[0]));
    if(param.thread_id!=0){
      pthread_cond_wait(&(cond_t_array[param.thread_id]), &the_mutex);
    }
  }
}

int main(int argc, char **argv)
{
  int N, status, clientfd;

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "USAGE: ./httpclient <hostname> <port> <threads> <request path>\n");
    return 1;
  }

  N = atoi(argv[3]);

  Requester_Parameter param;
  param.host = argv[1];
  param.port = argv[2];
  param.file_descriptor1 = argv[5];
  param.most_recent_file = 1;
  if (argc == 7)
  {
    param.file_descriptor2 = argv[6];
    param.file_count = 2;
  }
  else
  {
    param.file_count = 1;
    param.file_descriptor2 = NULL;
  }
  pthread_mutex_init(&the_mutex, 0);
  pthread_barrier_init(&barrier, NULL, N);
  sem_init(&fifo, 0, 1);
  cond_t_array = malloc((N + 1) * sizeof(pthread_cond_t));
  pthread_t thread_ids[N]; //Should be of size N
  for (int i = 0; i < N; i++)
  {
    if (!strcmp(argv[4], "CONCUR"))
    {
      status = pthread_create(&thread_ids[i], NULL, concur_requester, (void *)&param);
    }
    else if (!strcmp(argv[4], "FIFO"))
    {
      Requester_Parameter param_unique;
      param_unique.clientfd = param.clientfd;
      param_unique.file_count = param.file_count;
      param_unique.file_descriptor1 = param.file_descriptor1;
      param_unique.file_descriptor2 = param.file_descriptor2;
      param_unique.host = param.host;
      param_unique.most_recent_file = param.most_recent_file;
      param_unique.port = param.port;
      param_unique.thread_id = i;

      pthread_cond_t cond;
      pthread_cond_init(&cond, 0);
      cond_t_array[i] = cond;

      status = pthread_create(&thread_ids[i], NULL, fifo_requester, (void *)&param_unique);
    }
    else
    {
      fprintf(stderr, "Invalid Scheduling Argument\n");
      return 1;
    }
  }
  if (!strcmp(argv[4], "FIFO"))
  {
    cond_t_array[N] = cond_t_array[0];
    pthread_cond_signal(&cond_t_array[0]);
  }

  for (int i = 0; i < N; i++)
  {
    pthread_join(thread_ids[i], NULL);
  }
  printf("%d\n", 1);
}
