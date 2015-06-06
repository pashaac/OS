#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "helpers.h"

ssize_t read_(int fd, void * buf, size_t count) {
    size_t offset = 0;
    while (offset < count) {
        ssize_t read_block = read(fd, buf + offset, count - offset);
        if (read_block < 0) 
            return -1;
        if (read_block == 0) 
            break;
        offset += read_block;
    }
    return offset;
}

ssize_t write_(int fd, const void * buf, size_t count) {
    size_t offset = 0;
    while (offset < count) {
        ssize_t write_block = write(fd, buf + offset, count - offset);
        if (write_block < 0) 
            return -1;
        offset += write_block;
    }
    return offset;
}

ssize_t read_until(int fd, void * buf, size_t count, char delimiter) 
{    
    size_t offset = 0;
    while (offset < count) 
    {
        ssize_t read_block = read(fd, buf + offset, count - offset);
        if (read_block < 0) 
        {
            return read_block;
        }
        if (read_block == 0) 
        {
            break;
        }
        for (size_t i = offset; i != offset + read_block; ++i)
        {
            if (((char *)buf)[i] == delimiter)
            {
                return offset + read_block;
            }
        }
        offset += read_block;
    }
    return offset;
}

int spawn(const char * file, char * const argv []) 
{
    pid_t parent = fork();
    if (parent == -1) 
    {
        return parent;
    }

    if (parent)
    {
        int status;
        pid_t pid = wait(&status);
        if (pid == -1)
        {
            return pid;
        }
        return WEXITSTATUS(status);
    }
    else
    {
        return execvp(file, argv);
    }
}

typedef struct execargs_t execargs_t;

struct execargs_t* create_execargs(char * args[])
{
    struct execargs_t *ans = (struct execargs_t *) malloc(sizeof(struct execargs_t));

    if (ans == NULL)
    {
        exit(EXIT_FAILURE);
    }

    ans->prog_args = args;
    ans->prog_name = args[0];

    return ans;
}

int exec(struct execargs_t* args)
{
    return execvp(args->prog_name, args->prog_args);
}

pid_t *pids;
size_t n_pids;

void kill_all()
{
    for (size_t i = 0; i < n_pids; ++i)
    {
        if (pids[i] != -1)
        {
            kill(pids[i], SIGTERM);
        }
    }
    n_pids = 0;
}

void kill_handler(int code)
{
    code = 0;
    kill_all();
}

void sig_init(struct sigaction *sig_action, sigset_t *sig_set)
{
    memset(sig_action, 0, sizeof(struct sigaction));
    sigemptyset(sig_set);
    sigaddset(sig_set, SIGINT);
    sig_action->sa_mask = (*sig_set);
    sig_action->sa_handler = kill_handler;

}

void exit_handler(int code)
{
    code = 0;
    exit(EXIT_FAILURE);
}

void other_sig_init(struct sigaction *other_sig_action, sigset_t *other_sig_set)
{
    memset(other_sig_action, 0, sizeof(struct sigaction));
    sigemptyset(other_sig_set);
    sigaddset(other_sig_set, SIGPIPE);
    other_sig_action->sa_mask = (*other_sig_set);
    other_sig_action->sa_handler = exit_handler;

}

int runpiped(struct execargs_t** programs, size_t n)
{  
    if (!n)
    {
        return 0;
    }

    pids = (pid_t *) malloc (sizeof(pid_t) * n);
    if (pids == NULL)
    {
        return EXIT_FAILURE;
    }
    n_pids = 0;

    for (int i = 0; i < n; ++i)
    {
        pids[i] = -1;
    }

    struct sigaction sig_action;
    sigset_t sig_set;
    sig_init(&sig_action, &sig_set);
    sigaction(SIGINT, &sig_action, NULL);

    int fds[n - 1][2];
    for (int i = 0; i < n - 1; ++i)
    {
        if (pipe2(fds[i], O_CLOEXEC) == -1)
        {
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        pid_t id = fork();

        if (id < 0)
        {
            kill_all();
            for (int i = 0; i < n - 1; ++i)
            {
                if (close(fds[i][0]) == -1)
                {
                    return EXIT_FAILURE;
                }
                if (close(fds[i][1]) == -1)
                {
                    return EXIT_FAILURE;
                }
            }
            return EXIT_FAILURE;
        }

        if (id)
        {

            pids[n_pids] = id;
            ++n_pids;
        }
        else
        {
            if (i > 0)
            {
                if (dup2(fds[i - 1][0], 0) == -1)
                {
                    exit(EXIT_FAILURE);
                }
            }
            if (i < n - 1)
            {
                if (dup2(fds[i][1], 1) == -1)
                {
                    exit(EXIT_FAILURE);
                }
            }
            struct sigaction other_sig_action;
            sigset_t other_sig_set;
            other_sig_init(&other_sig_action, &other_sig_set);
            sigaction(SIGPIPE, &other_sig_action, NULL);
            exec(programs[i]);
        }
    }

    for (int i = 0; i < n - 1; ++i)
    {
        if (close(fds[i][0]) == -1)
        {
            return EXIT_FAILURE;
        }
        if (close(fds[i][1]) == -1)
        {
            return EXIT_FAILURE;
        }
    }

    
    while (1)
    {   
        int return_status;
        if ((wait(&return_status) < 0) && (errno == ECHILD))
        {
            break;
        }
    }

    return 0;
}

