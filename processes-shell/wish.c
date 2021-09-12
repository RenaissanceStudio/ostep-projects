// WIP

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

static void print_error();
static void run_cmd(char *path, char **cmds);
static char **extract_params(char *str, int *p_cnt);

char *strerror(int error)
{
    static char mesg[30];

    if (error >= 0 && error <= sys_nerr)
        return ((char *)sys_errlist[error]);

    sprintf(mesg, "Unknown error (%d)", error);
    return (mesg);
}

int dup2_ext(int fd1, int fd2)
{
    int rc;

    if ((rc = dup2(fd1, fd2)) < 0)
        print_error();
    return rc;
}

static int printable = 0;
#define RWRWRW (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

int main(int argc, char *argv[])
{
    // Goal: should implement exit, cd, and path as built-in commands.
    FILE *in = stdin;
    int with_prompt = 1;
    if (argc >= 2)
    {
        if (argc > 2)
        {
            print_error();
            exit(1);
        }

        if (printable)
            printf(">>> batch file : %s\n", argv[1]);

        FILE *fp;
        if ((fp = fopen(argv[1], "r")) == NULL)
        {
            if (printable)
                fprintf(stderr, "%s: cannot open file '%s'\n", argv[0], argv[1]);
            print_error();
            exit(1);
        }
        with_prompt = 0;
        in = fp;
    }

    if (with_prompt)
        printf("wish> ");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, in)) > 0)
    {
        if (linelen == -1) // EOF
            exit(0);

        if (line[strspn(line, " \t\v\r\n")] == '\0')
            continue;

        // remove the '\n' if any
        line[strcspn(line, "\n")] = 0;

        if (printable)
            printf(">>> line : %s | read len : %ld\n", line, linelen);

        char *cmd;
        char **out_params = NULL;
        int param_cnt = 0;
        out_params = extract_params(line, &param_cnt);

        cmd = out_params[0];
        // printf("cmd: %s | len : %lu\n", cmd, strlen(cmd));
        int i;
        int redirect_pos = -1;
        for (i = 0; i < (param_cnt + 1); i++)
        {
            if (out_params[i] != NULL && !strcmp(">", out_params[i]))
            {
                redirect_pos = i;
                break;
            }
        }

        if (redirect_pos > 0)
        {
            if (redirect_pos != param_cnt - 2)
            {
                // output file is missing or invalid
                print_error();
                continue;
            }
        }

        char *last_param = out_params[param_cnt - 1];
        char *pt_redirect = NULL;
        char *pt_out = NULL;
        if ((pt_redirect = strchr(last_param, '>')) != NULL)
        {
            pt_out = pt_redirect + 1;
            char outbuf[BUFSIZ];

            int cnt = strlen(pt_out);
            strncpy(outbuf, pt_out, cnt);
            outbuf[cnt] = 0;

            if (printable)
                printf("out file : %s\n", outbuf);

            int outfd;
            umask(0);
            if ((outfd = open(outbuf, O_RDWR | O_CREAT | O_TRUNC, RWRWRW)) < 0)
            {
                if (printable)
                    printf("failed to open file %s : %s\n", outbuf, strerror(errno));
                print_error();
                exit(1);
            }
            dup2_ext(STDOUT_FILENO, outfd); // old, new
            dup2_ext(STDERR_FILENO, outfd);

            last_param[pt_redirect - last_param] = '\0';

            if (printable)
                printf(">>> last param : %s\n", last_param);
        }

        if (!strcmp("exit", cmd))
        {
            if (param_cnt > 1)
            {
                print_error();
                continue;
            }
            // should just match "exit"
            exit(0);
        }

        if (!strcmp("cd", cmd))
        {
            if (param_cnt != 2)
                print_error();
            else
            {
                int ret;
                char *path = out_params[1];
                if ((ret = chdir(path)) != 0)
                {
                    print_error();
                    exit(1);
                };
            }
            continue;
        }

        if (!strcmp("path", cmd))
        {
            if (param_cnt > 1)
                setenv("wish_path", out_params[1], 1);
            else
                setenv("wish_path", "", 1);

            continue;
        }

        // check unknown commands
        char buf[64], usr_buf[64], curr_buf[64];
        sprintf(buf, "%s%s", "/bin/", cmd);
        sprintf(usr_buf, "%s%s", "/usr/bin/", cmd);

        char *cmd_path;

        char *my_path = getenv("wish_path");
        if (my_path == NULL) // fallback with system path
        {
            if (!access(buf, X_OK))
                cmd_path = buf;
            else if (!access(usr_buf, X_OK))
                cmd_path = usr_buf;
            else if (!access(curr_buf, X_OK))
                cmd_path = curr_buf;
            else // No x premission ?
            {
                // fprintf(stderr, "Unknown command : %s", cmd);
                print_error();
                continue;
            };
        }
        else
        {
            if (!strcmp("", my_path))
            { // already set path to be empty, only 'built-in' commands allowed
                print_error();
                continue;
            }

            // use the overridden path
            sprintf(curr_buf, "%s/%s", my_path, cmd);
            cmd_path = curr_buf;
        }

        int rc = fork();
        if (rc < 0)
        { // fork failed; exit
            fprintf(stderr, "fork failed\n");
            exit(1);
        }
        else if (rc == 0)
        {
            run_cmd(strdup(cmd_path), out_params);
        }
        else
        { // parent goes down this path (main)
            int rc_wait = wait(NULL);
            if (printable)
                printf(">>> wait val - rc : %d\n", rc_wait);

            if (with_prompt)
                printf("wish> ");
        }
    }

    return 0;
}

static void print_error()
{
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

static void run_cmd(char *path, char **cmds)
{
    execv(path, cmds);
}

static char **extract_params(char *str, int *p_cnt)
{
    // https://stackoverflow.com/questions/11198604/c-split-string-into-an-array-of-strings
    char **res = NULL;
    const char delimit[] = " \t\r\n"; //>
    char *p = strtok(str, delimit);
    int n_spaces = 0, i;

    /* split string and append tokens to 'res' */

    while (p)
    {
        res = realloc(res, sizeof(char *) * ++n_spaces);

        if (res == NULL)
            exit(-1); /* memory allocation failed */

        res[n_spaces - 1] = p;

        p = strtok(NULL, delimit);
    }

    /* realloc one extra element for the last NULL */

    res = realloc(res, sizeof(char *) * (n_spaces + 1));
    res[n_spaces] = 0;

    *p_cnt = n_spaces;

    /* print the result */

    if (printable)
    {
        for (i = 0; i < (n_spaces + 1); ++i)
            printf("res[%d] = %s\n", i, res[i]);
    }

    return res;
}