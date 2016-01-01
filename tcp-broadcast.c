#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

void listen_on(const unsigned short port);
void process_connections(const int lsock);

int main(const int argc, const char **argv)
{
  int port;

  if (argc != 2) {
    fprintf(stderr, "%s: usage: %s <port>\n", argv[0], argv[0]);
    return 1;
  }

  port = atoi(argv[1]);
  if (port < 1 || port > 65535) {
    fprintf(stderr, "%s: invalid port: %s\n", argv[0], argv[1]);
    return 2;
  }

  listen_on((unsigned short) port);

  return 0;
}

void listen_on(const unsigned short port)
{
  int lsock;
  struct sockaddr_in lsockaddr;

  lsock = socket(PF_INET, SOCK_STREAM, 0);
  memset(&lsockaddr, 0, sizeof(lsockaddr));
  lsockaddr.sin_family = AF_INET;
  lsockaddr.sin_port = htons(port);
  lsockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(lsock, (struct sockaddr *) &lsockaddr, sizeof(lsockaddr)) == -1) {
    perror("bind() failed");
    exit(3);
  }

  if (listen(lsock, 16) == -1) {
    perror("listen() failed");
    exit(4);
  }

  process_connections(lsock);
}

void process_connections(int lsock)
{
  printf("Listening on %d.\n", lsock);
}
