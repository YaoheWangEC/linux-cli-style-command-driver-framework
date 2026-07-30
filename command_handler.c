#include "command_handler.h"
#include "command_table.h"
#include "bsp.h"
#include "global_vars.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char resp[1024];

char* lscmd_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && 
       (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        sprintf(resp,
            "Usage: lscmd [options]\r\n"
            "Options:\r\n"
            "  -h, --help    Show this help message\r\n"
            "\r\n"
            "List all supported commands.\r\n");
        return resp;
    }
    
    if (argc == 1)
    {
        // 默认行为：列出所有命令
        int count = command_table_count();
        int offset = snprintf(resp, sizeof(resp), "Supported commands:\r\n");
        for (int i = 0; i < count && offset < sizeof(resp); i++)
        {
            const char* name = command_table_get_name(i);
            if (name)
            {
                offset += snprintf(resp + offset, sizeof(resp) - offset, "  %s\r\n", name);
            }
        }
        return resp;
    }

    // 无效参数
    sprintf(resp, "Invalid option. Try 'lscmd -h'\r\n");
    return resp;
}

char* echo_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        snprintf(resp, sizeof(resp),
                 "Usage: echo [text]\r\n"
                 "Options:\r\n"
                 "  -h, --help    Show this help message\r\n"
                 "\r\n"
                 "Print the given text to output.\r\n");
        return resp;
    }

    // 默认行为：拼接参数并返回
    if (argc > 1)
    {
        resp[0] = '\0';
        for (int i = 1; i < argc; i++)
        {
            strncat(resp, argv[i], sizeof(resp) - strlen(resp) - 1);
            if (i < argc - 1)
                strncat(resp, " ", sizeof(resp) - strlen(resp) - 1);
        }
        strncat(resp, "\r\n", sizeof(resp) - strlen(resp) - 1);
        return resp;
    }

    snprintf(resp, sizeof(resp), "\r\n");
    return resp;
}
