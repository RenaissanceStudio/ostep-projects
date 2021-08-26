#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <sys/stat.h>

typedef struct line
{
    char *str;
    struct line *next;
} Linestruct;

Linestruct *concate(char *input, Linestruct **pre)
{
    size_t len = strlen(input);
    Linestruct *ls = (Linestruct *)malloc(sizeof(Linestruct));
    if (ls == NULL)
    {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    char *content = (char *)malloc((len + 1) * sizeof(char));
    if (content == NULL)
    {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    strcpy(content, input);
    content[len] = '\0';

    ls->str = content;
    ls->next = pre == NULL ? NULL : *pre;

    return ls;
}

void check_valid_files(const char *input, const char *output)
{
    if (!strcmp(input, output))
    {
        fprintf(stderr, "reverse: input and output file must differ\n");
        exit(1);
    }

    const char path_separator = '/'; // os dependent?!

    char *sep_in = strrchr(input, path_separator);
    char *sep_out = strrchr(output, path_separator);

    // same name under different folders
    if (sep_in != NULL && sep_out != NULL && !strcmp(sep_in, sep_out))
    {
        fprintf(stderr, "reverse: input and output file must differ\n");
        exit(1);
    }
}

int debug = 0;

int main(int argc, char const *argv[])
{
    if (debug)
    {
        for (size_t i = 0; i < argc; i++)
        {
            printf("argv : %s ", argv[i]);
        }
        printf("argc : %d\n", argc);
    }
    if (argc > 3)
    {
        fprintf(stderr, "usage: reverse <input> <output>\n");
        exit(1);
    }
    else
    {
        FILE *fout = stdout;
        FILE *fin = NULL;

        if (argc == 1)
        {
            struct stat sb;
            fstat(fileno(stdin), &sb);
            if (S_ISREG(sb.st_mode)) // detect if stdin is an input file
            {
                fin = stdin;
            }
            else
            {
                fprintf(stderr, "usage: reverse <input> <output>\n");
                exit(1);
            }
        }
        // argc : 2,3
        const char *input = argv[1];
        if (argc == 3)
        {
            const char *output = argv[2];
            if ((fout = fopen(output, "w")) == NULL)
            {
                fprintf(stderr, "reverse: cannot open file '%s'\n", output);
                exit(1);
            }

            check_valid_files(input, output);
        }

        if (fin == NULL)
        {
            FILE *fp;
            if ((fp = fopen(input, "r")) == NULL)
            {
                fprintf(stderr, "reverse: cannot open file '%s'\n", input);
                exit(1);
            }

            fin = fp;
        }

        int readcnt = 0;
        char *line = NULL;
        size_t linecap = 0;
        ssize_t linelen = 0;

        Linestruct *pre = NULL;
        Linestruct *curr = NULL;
        while ((readcnt = getline(&line, &linecap, fin)) > 0)
        {
            curr = concate(line, &pre);
            pre = curr;
        }

        Linestruct *header = pre;
        while (pre != NULL)
        {
            fwrite(pre->str, strlen(pre->str), 1, fout);
            pre = pre->next;
        }

        // free all the allocated space
        while (header != NULL)
        {
            curr = header->next;

            free(header->str);
            header->next = NULL;
            free(header);

            header = curr;
        }

        fclose(fin);
        fclose(fout);
    }

    if (debug)
        printf(">>> reverse complete\n");

    exit(0);
}
