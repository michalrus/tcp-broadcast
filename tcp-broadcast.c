#include <stdio.h>
#include <stdlib.h>

void listen_on(const unsigned short port);

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
  printf("%d\n", port);
}
