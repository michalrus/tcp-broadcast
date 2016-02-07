#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PING_AFTER 5
#define TMOUT_SECS 5

struct client_data {
  char buf[4096];               /* this implies the max line length */
  size_t buf_len;
  char ident[128];
  time_t last_seen;
  int awaiting_pong;
};

int lsock = -1;                 /* has to be global for the sighandler :-( */
char *unix_socket_path = NULL;  /* has to be global for the sighandler :-( */

static void sighandler(int signo);
void listen_on(const unsigned short port
               /*, const char* unix_socket_path */ );
void process_connections(void /*const int lsock */ );
void accept_connection(fd_set * status, /*int lsock, */
                       struct client_data **clients);
void handle_data(fd_set * status, int csock, struct client_data **clients);
void handle_line(fd_set * status, int csock, struct client_data **clients);
void handle_client_exit(fd_set * status, int csock,
                        struct client_data **clients);
void do_write(const char *buf, fd_set * status, int csock,
              struct client_data **clients);

int main(const int argc, /*const */ char **argv)
{
  int port;

  if (argc != 2) {
    fprintf(stderr, "%s: usage: %s <port>/<unix-domain-socket>\n", argv[0],
            argv[0]);
    return 1;
  }

  port = atoi(argv[1]);
  if (port < 1 || port > 65535) {
    port = -1;
    unix_socket_path = argv[1];
  }

  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);
  signal(SIGPIPE, SIG_IGN);     /* ignore all SIGPIPEs (from write()s to
                                   sockets closed by clients) */

  listen_on((unsigned short) port);

  return 0;
}

static void sighandler(int signo)
{
  fprintf(stderr, "\nSignal %d caught, cleaning up.\n", signo);
  close(lsock);
  if (unix_socket_path != NULL)
    unlink(unix_socket_path);
}

void listen_on(const unsigned short port
               /*, const char* unix_socket_path */ )
{
  /*int lsock; */
  int bind_rc = -1;

  if (unix_socket_path != NULL) {
    struct sockaddr_un lsockaddr;
    memset(&lsockaddr, 0, sizeof(lsockaddr));
    lsock = socket(AF_UNIX, SOCK_STREAM, 0);
    lsockaddr.sun_family = AF_UNIX;
    strncpy(lsockaddr.sun_path, unix_socket_path,
            sizeof(lsockaddr.sun_path) - 1);
    bind_rc =
        bind(lsock, (struct sockaddr *) &lsockaddr, sizeof(lsockaddr));
  } else {
    struct sockaddr_in lsockaddr;
    memset(&lsockaddr, 0, sizeof(lsockaddr));
    lsock = socket(PF_INET, SOCK_STREAM, 0);
    lsockaddr.sin_family = AF_INET;
    lsockaddr.sin_port = htons(port);
    lsockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_rc =
        bind(lsock, (struct sockaddr *) &lsockaddr, sizeof(lsockaddr));
  }

  if (bind_rc == -1) {
    perror("bind() failed");
    exit(3);
  }

  if (listen(lsock, 16) == -1) {
    perror("listen() failed");
    exit(4);
  }

  process_connections( /*lsock */ );
}

void process_connections(void /*const int lsock */ )
{
  int fd;
  fd_set status, current;
  struct client_data *clients[FD_SETSIZE];

  memset(clients, 0, sizeof(clients));

  FD_ZERO(&status);
  FD_SET(lsock, &status);
  for (;;) {
    struct timeval tmout;
    tmout.tv_sec = 1;
    tmout.tv_usec = 0;
    current = status;           /* I copy the original fd_set every time,
                                   because select() obviously clears fds
                                   that are not readable; and in the next
                                   run requires them to be set again. */
    if (select(FD_SETSIZE, &current, NULL, NULL, &tmout) == -1) {
      if (errno == EINTR)
        exit(0);
      perror("select() failed");
      exit(5);
    }

    for (fd = 0; fd < FD_SETSIZE; fd++) {
      if (FD_ISSET(fd, &current)) {
        if (fd == lsock)
          accept_connection(&status, /*lsock, */ clients);
        else
          handle_data(&status, fd, clients);
      }
      /* discover dropped connections */
      if (fd != lsock && FD_ISSET(fd, &status)) {       /* i.e. if the client exists */
        time_t now = time(NULL);
        int since_last_seen = now - clients[fd]->last_seen;
        if (since_last_seen >= PING_AFTER && !clients[fd]->awaiting_pong) {
          do_write("ping\n", &status, fd, clients);
          clients[fd]->awaiting_pong = 1;
        } else if (since_last_seen >= PING_AFTER + TMOUT_SECS) {
          do_write("bye Timed out, please respond to ping's.\n", &status,
                   fd, clients);
          handle_client_exit(&status, fd, clients);
        }
      }
    }
  }
}

void accept_connection(fd_set * status, /*int lsock, */
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
  } else {
    FD_SET(csock, status);
    clients[csock] = malloc(sizeof(struct client_data));        /* FIXME: is NULL? C'mon... */
    memset(clients[csock], 0, sizeof(struct client_data));
    clients[csock]->last_seen = time(NULL);
    sprintf(clients[csock]->ident,      /* sizeof(ident) == 128 should be enough,
                                           I really want ANSI... */
            "%s:%hu", inet_ntoa(csockaddr.sin_addr),
            ntohs(csockaddr.sin_port));
    /* fprintf(stderr, "[%s] new connection\n", clients[csock]->ident); */
    do_write("ping\n", status, csock, clients);
  }
}

void handle_data(fd_set * status, int csock, struct client_data **clients)
{
  struct client_data *c = clients[csock];
  int num_read =
      read(csock, c->buf + c->buf_len, sizeof(c->buf) - c->buf_len - 1);
  if (num_read <= 0)
    handle_client_exit(status, csock, clients);
  else {
    char *line_end;
    c->last_seen = time(NULL);
    c->awaiting_pong = 0;       /* you sure this is semantically correct? */
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

  /* fprintf(stderr, "[%s] cmd = \"%s\" && arg = \"%s\"\n", c->ident, cmd,
     arg); */

  if (strcmp(cmd, "ping") == 0) {
    do_write("pong\n", status, csock, clients);
  } else if (strcmp(cmd, "pong") == 0) {
    c->awaiting_pong = 0;
  } else if (strcmp(cmd, "quit") == 0) {
    do_write("bye\n", status, csock, clients);
    handle_client_exit(status, csock, clients);
  } else if (strcmp(cmd, "broadcast") == 0 && arg != NULL) {
    int fd;
    for (fd = 0; fd < FD_SETSIZE; fd++)
      if (clients[fd] != NULL) {
        do_write("broadcast ", status, fd, clients);
        do_write(c->ident, status, fd, clients);
        do_write(" ", status, fd, clients);
        do_write(arg, status, fd, clients);
        do_write("\n", status, fd, clients);    /* I do still want ANSI :-) */
      }
  } else {
    do_write
        ("unknown Available commands: broadcast <msg>, ping, pong, quit.\n",
         status, csock, clients);
  }
}

void handle_client_exit(fd_set * status, int csock,
                        struct client_data **clients)
{
  if (clients[csock]) {
    /* fprintf(stderr, "[%s] disconnected\n", clients[csock]->ident); */
    free(clients[csock]);
    clients[csock] = NULL;
  }
  FD_CLR(csock, status);
  close(csock);
}

void do_write(const char *buf, fd_set * status, int csock,
              struct client_data **clients)
{
  int num_written = write(csock, buf, strlen(buf));
  if (num_written < 0)
    handle_client_exit(status, csock, clients);
}
