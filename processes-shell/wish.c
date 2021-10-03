#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

static int printable = 0;
static const char delimit[] = " \t\v\r\n";
#define RWRWRW (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

void print_error();

void run_cmd(char *path, char **cmds);

char **extract_params(const char *str, int *p_cnt, const char *delimit);

int dup2_ext(int fd1, int fd2);

int cmd_cd(char **params, int cnt);

int cmd_exit(char **params, int cnt);

int cmd_path(char **params, int cnt);

void redirect_to(const char *out_path_file);

void fork_and_run(int with_prompt, char **out_params, int param_cnt);

void fork_run_sub_commands(char *line);

const char *builtin_cmds[] = {
    "cd",
    "exit",
    "path",
};

int builtin_len()
{
    return sizeof(builtin_cmds) / sizeof(char *);
}

int (*builtin_func[])(char **args, int len) = {
    cmd_cd,
    cmd_exit,
    cmd_path,
};

pid_t r_wait(int *stat_loc)
{
    int ret;
    while ((ret = wait(stat_loc)) == -1 && (errno == EINTR))
        ;
    return ret;
}

char *generate_cmd_path(char *cmd)
{
    while (*cmd == ' ')
        cmd++;

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
            return NULL;
        };
    }
    else
    {
        if (!strcmp("", my_path))
        { // already set path to be empty, only 'built-in' commands allowed
            print_error();
            return NULL;
        }

        // use the overridden path
        if (printable)
            printf(">>> extract custom paths : <<<\n");

        int path_cnt;
        char **paths = extract_params(my_path, &path_cnt, ":");
        if (printable)
            printf(">< path cnt : %d\n", path_cnt);

        int i;
        for (i = 0; i < path_cnt; i++)
        {
            char *path = paths[i];
            if (path != NULL)
            {
                sprintf(curr_buf, "%s/%s", path, cmd);
                if (!access(curr_buf, X_OK))
                {
                    cmd_path = curr_buf;
                    break;
                }
            }
        }
        if (printable)
        {
            printf("cmd full path : %s\n", cmd_path);
        }
    }
    return strdup(cmd_path);
}

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
    ssize_t linelen;
    size_t linecap = 0;

    while ((linelen = getline(&line, &linecap, in)) > 0)
    {
        if (linelen == -1) // EOF
            exit(0);

        if (line[strspn(line, delimit)] == '\0')
            continue;

        // remove the '\n' if any
        line[strcspn(line, "\n")] = 0;

        if (printable)
            printf(">>> line : %s | read len : %ld\n", line, linelen);

        char *line_orgin = strdup(line);

        char *cmd;
        char **out_params = NULL;
        int param_cnt = 0;
        out_params = extract_params(line, &param_cnt, delimit);

        cmd = out_params[0];

        // process IO redirection
        int i;
        int redirect_pos = -1;
        int parallel_pos = -1;
        for (i = 0; i < (param_cnt + 1); i++)
        {
            if (out_params[i] == NULL)
                continue;
            if (redirect_pos < 0 && !strcmp(">", out_params[i]))
                redirect_pos = i;

            char *parallel_sep = strchr(out_params[i], '&');
            if (parallel_sep != NULL)
                parallel_pos = i;
        }

        // PreCheck : cmd line with  a simple redirection
        if (redirect_pos > 0)
        {
            if (parallel_pos < 0 && redirect_pos != param_cnt - 2)
            { // error : output file is missing or invalid
                print_error();
                continue;
            }
        }

        // built-in commands
        int status = 0;
        for (i = 0; i < builtin_len(); i++)
        {
            if (strcmp(cmd, builtin_cmds[i]) == 0)
            {
                status = builtin_func[i](out_params, param_cnt);
                break;
            }
        }
        if (status == 1)
            continue;

        if (!strcmp("&", cmd))
            continue;

        if (parallel_pos >= 0)
        {
            fork_run_sub_commands(line_orgin);

            // wait for all the children
            while (r_wait(NULL) > 0)
                ;
            continue;
        }

        fork_and_run(with_prompt, out_params, param_cnt);
    }

    return 0;
}

void fork_run_sub_commands(char *line)
{
    int sub_cmd_len = 0;
    char **sub_cmds = extract_params(line, &sub_cmd_len, "&");
    if (printable)
        printf("sub cmds len : %d\n", sub_cmd_len); // 3
    int m = 0;
    for (m = 0; m < sub_cmd_len; m++)
    {
        char *sub_cmd = sub_cmds[m];
        if (printable)
            printf(">>> sub cmd : %s\n", sub_cmd);

        int rc = fork();
        if (rc < 0)
        {
            // fork failed; exit
            fprintf(stderr, "fork failed\n");
            exit(1);
        }
        else if (rc == 0)
        {
            // e.g. p5.sh > x.out
            int len = 0;
            char **sub_params = extract_params(sub_cmd, &len, " >");

            char *p_red = strchr(sub_cmd, '>');
            if (p_red != NULL)
            {
                redirect_to(sub_params[len - 1]);

                *p_red = '\0';
                free(sub_params);

                sub_params = extract_params(sub_cmd, &len, " ");
            }
            int i = 0;
            while (sub_params[i] != NULL)
            {
                if (printable)
                {
                    printf("<<< sub cmds final ->: %s\n", sub_params[i]);
                }
                i++;
            }

            run_cmd(generate_cmd_path(sub_params[0]), sub_params);
        }
        else
        {
            continue;
        }
    }
}

void fork_and_run(int with_prompt, char **out_params, int param_cnt)
{
    int rc = 0;
    char *cmd = out_params[0];
    char *cmd_path = generate_cmd_path(cmd);

    rc = fork();
    if (rc < 0)
    { // fork failed; exit
        fprintf(stderr, "fork failed\n");
        exit(1);
    }
    else if (rc == 0)
    {
        if (printable)
            printf(">>> in child proc\n");

        // e.g. ls tests/p2a-test>/tmp/output11
        int j;
        for (j = 1; j < param_cnt; j++)
        {
            int len = 0;
            char *sub_cmd = out_params[j];
            char *p_red = strchr(sub_cmd, '>');
            if (p_red != NULL)
            {
                char **sub_params = extract_params(sub_cmd, &len, " >");
                redirect_to(sub_params[len - 1]);

                *p_red = '\0'; // closing the cmd as redirecting IO completed
            }
        }

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

void redirect_to(const char *out_path_file)
{
    char outbuf[BUFSIZ];

    int cnt = strlen(out_path_file);
    strncpy(outbuf, out_path_file, cnt);
    outbuf[cnt] = 0;

    if (printable)
        printf("redirect out file : %s\n", outbuf);

    int outfd;
    umask(0);
    if ((outfd = open(outbuf, O_RDWR | O_CREAT | O_TRUNC, RWRWRW)) < 0)
    {
        if (printable)
            printf("failed to open file %s : %s\n", outbuf, strerror(errno));
        print_error();
        exit(1);
    }

    // duplicate output file to stdout
    dup2_ext(outfd, STDOUT_FILENO);

    // duplicate output file to stderr
    dup2_ext(outfd, STDERR_FILENO);
}

int cmd_cd(char **params, int cnt)
{
    if (cnt != 2)
    {
        print_error();
    }
    else
    {
        int ret;
        char *path = params[1];
        if ((ret = chdir(path)) != 0)
        {
            print_error();
            exit(1);
        };
    }
    return 1;
}

int cmd_exit(char **params, int cnt)
{
    if (cnt > 1)
    {
        print_error();
        return 1;
    }
    // exit matched
    exit(0);
    return 0;
}

int cmd_path(char **params, int cnt)
{
    char buf[BUFSIZ];
    if (cnt > 1)
    {
        int read_cnt = 0;
        int i = 1;
        for (i = 1; i <= cnt; i++)
        {
            if (params[i] != NULL)
            {
                strcpy(buf + read_cnt, params[i]);
                read_cnt += strlen(params[i]);
                buf[read_cnt] = ':';
                read_cnt++;
            }
        }
        if (read_cnt > 0) // close the buf
            buf[read_cnt - 1] = '\0';

        if (printable)
            printf(">>> set path : %s\n", buf);

        setenv("wish_path", buf, 1);
    }
    else
    {
        setenv("wish_path", "", 1);
    }
    return 1;
}

void print_error()
{
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

void run_cmd(char *path, char **cmds)
{
    execv(path, cmds);
}

int dup2_ext(int fd1, int fd2)
{
    if (fd1 == fd2)
        return fd2;

    int rc;
    if ((rc = dup2(fd1, fd2)) < 0)
        print_error();
    return rc;
}

/***
 * Extracts the sequential tokens in a null-terminated string
 *
 * @param str source string
 * @param p_cnt  valid param length excluding the 'NULL'
 * @param delimit
 * @return an array of char*
 */
char **extract_params(const char *str, int *p_cnt, const char *delimit)
{
    if (str == NULL || delimit == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    char *t;
    const char *newstr;

    newstr = str + strspn(str, delimit); // skip the intial delimits if any
    if ((t = malloc(strlen(newstr) + 1)) == NULL)
        exit(-1);

    strcpy(t, newstr);

    // https://stackoverflow.com/questions/11198604/c-split-string-into-an-array-of-strings
    char **res = NULL;
    char *p = strtok(t, delimit);
    int n_spaces = 0, i;

    if (p == NULL)
        free(t);

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
        printf("{");
        for (i = 0; i < (n_spaces + 1); ++i)
            printf("[%d]%s ", i, res[i]);
        printf("}\n");
    }

    return res;
}