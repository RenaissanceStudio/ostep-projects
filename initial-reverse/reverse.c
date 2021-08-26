#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <sys/stat.h>
typedef struct line
{
    char *str;
    struct line *next;
} linestruct;

linestruct *concate(char *input, linestruct **pre)
{
    size_t len = strlen(input);
    linestruct *ls = (linestruct *)malloc(sizeof(linestruct));
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
        FILE *out = stdout;
        FILE *fin = NULL;

        if (argc == 1)
        {
            struct stat sb;
            fstat(fileno(stdin), &sb);
            if (S_ISREG(sb.st_mode)) // Detect if stdin is a input file
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
            if ((out = fopen(output, "w")) == NULL)
            {
                fprintf(stderr, "reverse: cannot open file '%s'\n", output);
                exit(1);
            }

            if (!strcmp(input, output))
            {
                fprintf(stderr, "reverse: input and output file must differ\n");
                exit(1);
            }

            // same name under different folders
            char *sep_in = strrchr(input, '/'); // path separator: os dependent?!
            char *sep_out = strrchr(output, '/');
            if (sep_in != NULL && sep_out != NULL && !strcmp(sep_in, sep_out))
            {
                fprintf(stderr, "reverse: input and output file must differ\n");
                exit(1);
            }
        }

        char *line = NULL;
        size_t linecap = 0;
        ssize_t linelen = 0;

        int readcnt = 0;

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

        linestruct *pre = NULL;
        linestruct *curr = NULL;
        while ((readcnt = getline(&line, &linecap, fin)) > 0)
        {
            curr = concate(line, &pre);
            pre = curr;
        }

        while (pre != NULL)
        {
            fwrite(pre->str, strlen(pre->str), 1, out);
            pre = pre->next;
        }

        fclose(fin);
        fclose(out);
    }

    if (debug)
        printf(">>> reverse complete\n");

    exit(0);
}
