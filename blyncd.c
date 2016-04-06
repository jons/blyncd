/**
 * @author jon <jon@wroth.org>
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <hidapi.h>
#include <ctype.h>

#define VENDOR_ID        (0x1130)
#define PRODUCT_ID       (0x0001)
#define SERVICE_PORT     ("4545")
#define SERVICE_BACKLOG  (9)

#define C_OFF     (0)
#define C_WHITE   (1)
#define C_MAGENTA (2)
#define C_BLUE    (3)
#define C_CYAN    (4)
#define C_GREEN   (5)
#define C_RED     (6)
#define C_YELLOW  (7)
#define NCOLORS   (8)

struct lightdev
{
  char alias;
  char *path;

  struct lightdev *next;
};

struct blync
{
  struct lightdev *lights;
  int sfd;

  pthread_mutex_t lock_lights;
};

struct client
{
  struct blync *state;
  int cfd;
  struct sockaddr_storage addr;
  socklen_t addrsz;
  pthread_t tid;
};


// color table
unsigned char colortab[] = { 255, 15, 47, 63, 31, 95, 111, 79 };

char *colormap[] =
{
  "off",
  "white",
  "magenta",
  "blue",
  "cyan",
  "green",
  "red",
  "yellow",
};

// program state
volatile int running = 1;
volatile int debug = 0;

/**
 * hid write report
 */
int hw_write (const char *path, const unsigned char *data, size_t len)
{
  int r;
  hid_device *dev = hid_open_path(path);

  if (NULL == dev)
  {
    perror("hid_open_path");
    return -1;
  }
  r = hid_write(dev, data, len);
  if (r < 0)
    perror("hid_write");
  if (r != len)
    perror("hid_write(short)");

  hid_close(dev);

  if (r > 0)
    r = (r == len) ? 0 : -1;
  return r;
}


/**
 * set a blync light color
 */
int setlight (const char *path, int idx)
{
  unsigned char setbuf[9] = { 0, 0x55, 0x53, 0x42, 0x43, 0, 0x40, 0x2, 0xff };
  setbuf[8] = colortab[idx];
  if (debug) printf("hw_write %s %d\n", path, idx);
  return hw_write(path, setbuf, 9);
}



/**
 * enumerate HIDs, filter for blync lights,
 * save USB HID path for each match
 */
void *hid_discovery(void *a)
{
  struct blync *state = (struct blync *)a;
  struct hid_device_info *hids, *hids_cursor;
  struct lightdev *lights = NULL, *cur, *prev = NULL;
  int ok = 1;

  // TODO: while(running) sleep-a-while
  // TODO: update rather than replace the device list


  hids_cursor = hids = hid_enumerate(VENDOR_ID, PRODUCT_ID);
  if (NULL != hids)
  {
    while (hids_cursor)
    {
      if (hids_cursor->interface_number == 1)
      {
        cur = malloc(sizeof (struct lightdev)); // TODO: verify not null
        if (NULL == cur)
        {
          ok = 0;
          break;
        }
        cur->alias = '.';
        cur->path = strdup(hids_cursor->path);  // TODO: verify not null
        cur->next = NULL;

        if (debug) printf("discover: %s\n", cur->path);

        if (NULL == lights)
          lights = cur;
        if (NULL != prev)
          prev->next = cur;
        prev = cur;
      }
      hids_cursor = hids_cursor->next;
    }
    hid_free_enumeration(hids);
  }


  if (ok)
  {
    // free old list, replace it with new list
    pthread_mutex_lock(&state->lock_lights);
    if (state->lights)
    {
      prev = state->lights;
      while (prev)
      {
        cur = prev->next;
        free(prev->path);
        free(prev);
        prev = cur;
      }
    }
    state->lights = lights;
    pthread_mutex_unlock(&state->lock_lights);
  }
  else
  {
    // free the new list, leave old list intact
    while (lights)
    {
      cur = lights->next;
      free(lights->path);
      free(lights);
      lights = cur;
    }
  }

  pthread_exit(NULL);
}


/**
 */
void *cli_connect(void *a)
{
  struct client *cli = (struct client *)a;
  struct lightdev *ptr;
  int i, r, search, color, txlen;
  char alias, lookahead = '\0';
  char txbuf[128] = { '\0' };
  char command[128] = { '\0' };

  //char s[INET6_ADDRSTRLEN];
  //inet_ntop(cli->addr.ss_family, &((struct sockaddr_in *)&cli->addr)->sin_addr, s, sizeof s);
  //printf("%s\n", s);


  while (running)
  {
    memset(command, 0, sizeof command);
    if (lookahead)
    {
      if (debug) printf("using lookahead %02hhx\n", lookahead);
      command[0] = lookahead;
      lookahead = '\0';
    }
    else
    {
      r = recv(cli->cfd, command, 1, 0);
      if (r <= 0) break;
    }
    if (debug) printf("command %02hhx\n", command[0]);

    // ignore whitespace
    if (isspace(command[0]))
    {
      continue;
    }

    // explicit client quit
    if ('.' == command[0])
    {
      break;
    }

    // set device alias
    if ('a' == command[0])
    {
      // read one character followed by a device id
      // ensure alias character isalpha, otherwise give it to lookahead
      memset(command, 0, sizeof command);
      r = recv(cli->cfd, command, 1, 0);
      if (r < 0) break;
      if (!isalpha(command[0]) && command[0] != '.')
      {
        lookahead = command[0];
        command[i] = '\0';
        if (debug) printf("set lookahead %02hhx\n", lookahead);
        continue;
      }
      alias = command[0];
      if (debug) printf("setalias %c\n", alias);

      memset(command, 0, sizeof command);
      for (i = 0; i < sizeof(command)-1 && i < 12; ) // xxxx:xxxx:xx
      {
        r = recv(cli->cfd, command+i, 1, 0);
        if (r < 0) break;
        if (r > 0)
        {
          if (i == 0 && command[0] == ',') continue; // optional, leave i=0 and overwrite

          if (!isxdigit(command[i]) && ':' != command[i])
          {
            lookahead = command[i];
            command[i] = '\0';
            if (debug) printf("set lookahead %02hhx\n", lookahead);
            break;
          }
        }
        i++; // but what choice do we have?
      }
      if (r < 0) break;
      if (!strlen(command)) continue;

      // iterate lightdev map looking for alias and device
      // - clear when found elsewhere
      // - assign to matching device id
      pthread_mutex_lock(&cli->state->lock_lights);
      ptr = cli->state->lights;
      while (ptr != NULL)
      {
        if (ptr->alias == alias)
          ptr->alias = '.';

        if (!strcmp(ptr->path, command))
          ptr->alias = alias;

        ptr = ptr->next;
      }
      pthread_mutex_unlock(&cli->state->lock_lights);
      continue;
    }

    // set color,device
    if ('s' == command[0])
    {
      // read color (and possibly one extra character)
      memset(command, 0, sizeof command);
      for (i = 0; i < sizeof(command)-1; )
      {
        r = recv(cli->cfd, command+i, 1, 0);
        if (r < 0) break;
        if (r > 0)
        {
          if (!isdigit(command[i]))
          {
            lookahead = command[i];
            command[i] = '\0';
            if (debug) printf("set lookahead %02hhx\n", lookahead);
            break;
          }
        }
        i++; // trust is hard sometimes
      }
      if (r < 0) break;
      color = strtol(command, NULL, 10);
      if (debug) printf("setcolor %x\n", color);
      search = 0;
      if (',' == lookahead)
      {
        lookahead = '\0';
        memset(command, 0, sizeof command);
        for (i = 0; i < sizeof(command)-1 && i < 12; ) // xxxx:xxxx:xx
        {
          r = recv(cli->cfd, command+i, 1, 0);
          if (r < 0) break;
          if (r > 0)
          {
            if (i == 0 && isalpha(command[0])) // assume its an alias
              break;

            if (!isxdigit(command[i]) && ':' != command[i]) // look for ID
            {
              lookahead = command[i];
              command[i] = '\0';
              if (debug) printf("set lookahead %02hhx\n", lookahead);
              break;
            }
          }
          i++; // but what choice do we have?
        }
        if (r < 0) break;
        search = strlen(command);
      }

      pthread_mutex_lock(&cli->state->lock_lights);
      ptr = cli->state->lights;
      while (ptr != NULL)
      {
        if (!search)
        {
          setlight(ptr->path, color);
        }
        else if (!strcmp(ptr->path, command) || ptr->alias == command[0])
        {
          setlight(ptr->path, color);
          break;
        }
        ptr = ptr->next;
      }
      pthread_mutex_unlock(&cli->state->lock_lights);
      continue;
    }

    if ('d' == command[0])
    {
      pthread_mutex_lock(&cli->state->lock_lights);
      ptr = cli->state->lights;
      while (ptr != NULL)
      {
        send(cli->cfd, ptr->path, strlen(ptr->path), 0);
        if (isprint(ptr->alias) && ptr->alias != '.')
        {
          memset(txbuf, 0, sizeof txbuf);
          txlen = snprintf(txbuf, sizeof(txbuf)-1, " %c", ptr->alias);
          send(cli->cfd, txbuf, txlen, 0);
        }
        send(cli->cfd, "\n", 1, 0);
        ptr = ptr->next;
      }
      pthread_mutex_unlock(&cli->state->lock_lights);
      continue;
    }

    if ('c' == command[0])
    {
      for (i = 0; i < NCOLORS; i++)
      {
        memset(txbuf, 0, sizeof txbuf);
        txlen = snprintf(txbuf, sizeof(txbuf)-1, "%d %s (0x%02hhx)\n", i, colormap[i], colortab[i]);
        send(cli->cfd, txbuf, txlen, 0);
      }
      continue;
    }

    memset(txbuf, 0, sizeof txbuf);
    txlen = snprintf(txbuf, sizeof(txbuf)-1, "unrecognized command: %c\n", command[0]);
    send(cli->cfd, txbuf, txlen, 0);
  }

  close(cli->cfd);
  free(cli);
  pthread_exit(NULL);
}


/**
 */
int service_create(struct blync *state)
{
  struct addrinfo *addr;
  struct addrinfo hints;
  char yes = 1;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  getaddrinfo(NULL, SERVICE_PORT, &hints, &addr);

  state->sfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

  setsockopt(state->sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(char));

  bind(state->sfd, addr->ai_addr, addr->ai_addrlen);

  freeaddrinfo(addr);

  return listen(state->sfd, SERVICE_BACKLOG);
}


/**
 */
void sig_interrupt(int n, siginfo_t *info, void *context)
{
  running = 0;
  if (debug) fprintf(stderr, "interrupt %d\n", n);
}


/**
 */
int main(int argc, char ** argv)
{
  pthread_t t_discovery;

  struct blync state =
  {
    .lights = NULL,
    .lock_lights = PTHREAD_MUTEX_INITIALIZER
  };

  struct sigaction sig_handler =
  {
    .sa_sigaction = sig_interrupt,
    .sa_flags     = SA_SIGINFO|SA_RESETHAND,
  };

  sigemptyset(&sig_handler.sa_mask);
  sigaction(SIGHUP, &sig_handler, NULL);
  sigaction(SIGINT, &sig_handler, NULL);
  sigaction(SIGQUIT, &sig_handler, NULL);

  if (argc > 1)
    debug = (0 == strcasecmp("-d", argv[1])) || (0 == strcasecmp("--debug", argv[1]));

  hid_init();

  service_create(&state);

  pthread_create(&t_discovery, NULL, hid_discovery, (void *)&state);

  while (running)
  {
    struct client *cli = malloc(sizeof (struct client));
    cli->state = &state;
    cli->cfd = accept(state.sfd, (struct sockaddr *)&cli->addr, &cli->addrsz);
    if (cli->cfd < 0)
    {
      free(cli);
      continue;
    }
    pthread_create(&cli->tid, NULL, cli_connect, (void *)cli);
  }


  pthread_join(t_discovery, NULL);


  // cleanup program state

  struct lightdev *lights_cursor = state.lights;
  while (lights_cursor)
  {
    lights_cursor = lights_cursor->next;
    free(state.lights->path);
    free(state.lights);
    state.lights = lights_cursor;
  }

  hid_exit();

  pthread_mutex_destroy(&state.lock_lights);

  return 0;
}
