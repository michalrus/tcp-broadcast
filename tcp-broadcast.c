#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct client_data {
  char buf[4096];               /* this implies the max line length */
  size_t buf_len;
  char ident[128];
};

void listen_on(const unsigned short port);
void process_connections(const int lsock);
void accept_connection(fd_set * status, int lsock,
                       struct client_data **clients);
void handle_data(fd_set * status, int csock, struct client_data **clients);
void handle_line(fd_set * status, int csock, struct client_data **clients);
void handle_client_error(fd_set * status, int csock,
                         struct client_data **clients);

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

void process_connections(const int lsock)
{
  int fd;
  fd_set status, current;
  struct client_data *clients[FD_SETSIZE];

  memset(clients, 0, sizeof(clients));

  FD_ZERO(&status);
  FD_SET(lsock, &status);

  for (;;) {
    current = status;           /* I copy the original fd_set every time,
                                   because select() obviously clears fds
                                   that are not readable; and in the next
                                   run requires them to be set again. */
    if (select(FD_SETSIZE, &current, NULL, NULL, NULL) == -1) {
      perror("select() failed");
      exit(5);
    }

    for (fd = 0; fd < FD_SETSIZE; fd++)
      if (FD_ISSET(fd, &current)) {
        if (fd == lsock)
          accept_connection(&status, lsock, clients);
        else
          handle_data(&status, fd, clients);
      }
  }
}

void accept_connection(fd_set * status, int lsock,
                       struct client_data **clients)
{
  struct sockaddr_in csockaddr;
  socklen_t csockaddr_len = sizeof(csockaddr);
  int csock;
  memset(&csockaddr, 0, csockaddr_len);
  csock = accept(lsock, (struct sockaddr *) &csockaddr, &csockaddr_len);
  if (csock == -1) {
    perror("accept() failed");
  } else if (csock >= FD_SETSIZE) {
    fprintf(stderr, "currently unable to accept new connections\n");
    close(csock);
  } else if (write(csock, "kupa\r\n", 6) >= 0) {
    FD_SET(csock, status);
    clients[csock] = malloc(sizeof(struct client_data));        /* TODO: free me somewhere
                                                                   FIXME: NULL? C'mon... */
    memset(clients[csock], 0, sizeof(struct client_data));
    sprintf(clients[csock]->ident,      /* sizeof(ident) == 128 should be enough,
                                           I really want ANSI... */
            "%s:%hu", inet_ntoa(csockaddr.sin_addr),
            ntohs(csockaddr.sin_port));
    fprintf(stderr, "[%s] new connection\n", clients[csock]->ident);
  }
}

void handle_data(fd_set * status, int csock, struct client_data **clients)
{
  struct client_data *c = clients[csock];
  int num_read =
      read(csock, c->buf + c->buf_len, sizeof(c->buf) - c->buf_len - 1);
  if (num_read <= 0)
    handle_client_error(status, csock, clients);
  else {
    char *line_end;
    c->buf_len += num_read;
    c->buf[c->buf_len] = '\0';
    for (;;) {                  /* there may be several lines in the buffer */
      line_end = strchr(c->buf, '\n');
      if (line_end == NULL && c->buf_len >= sizeof(c->buf) - 1) {
        /* no new line, but buffer full, we have to flush */
        c->buf[c->buf_len] = '\0';
        handle_line(status, csock, clients);
        c->buf_len = 0;
        c->buf[0] = '\0';
      } else if (line_end != NULL) {
        /* new line found */
        size_t next_line_offset = (line_end - c->buf + 1);
        size_t this_line_len = (line_end - c->buf);
        size_t i;
        if (this_line_len > 0 && c->buf[this_line_len - 1] == '\r')
          this_line_len--;
        c->buf[this_line_len] = '\0';
        handle_line(status, csock, clients);
        /* move the rest of the buffer to the begining; circular would be better... */
        for (i = 0; i + next_line_offset < c->buf_len; i++)
          *(c->buf + i) = *(c->buf + next_line_offset + i);
        c->buf_len -= next_line_offset;
      } else
        break;                  /* no new line, buffer not full, let's wait */
    }
  }
}

void handle_line(fd_set * status, int csock, struct client_data **clients)
{
  struct client_data *c = clients[csock];

  /* parse line into cmd and arg */
  char *cmd = c->buf, *arg;
  while (*cmd == ' ')
    cmd++;
  arg = strchr(cmd, ' ');
  if (arg != NULL)
    while (*arg == ' ') {
      *arg = '\0';
      arg++;
    }

  fprintf(stderr, "[%s] cmd = \"%s\" && arg = \"%s\"\n", c->ident, cmd,
          arg);

  if (FD_ISSET(csock, status)) {
    /* FIXME: remove this; added only to circumvent a warning */
  }
}

void handle_client_error(fd_set * status, int csock,
                         struct client_data **clients)
{
  if (clients[csock]) {
    fprintf(stderr, "[%s] disconnected\n", clients[csock]->ident);
    free(clients[csock]);
    clients[csock] = NULL;
  }
  FD_CLR(csock, status);
  close(csock);
}
