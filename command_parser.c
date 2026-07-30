#include "command_parser.h"
#include "command_reader.h"
#include "command_table.h"
#include "command_handler.h"
#include "global_vars.h"
#include "usb_core.h"
#include "cdc_class.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief 通过 USB 发送响应字符串（分段发送，每次最多 64 字节）
 *
 * @param msg 要发送的字符串
 */
static void usb_send_response(const char *msg)
{
    size_t total_len = strlen(msg);
    size_t offset = 0;

    while (offset < total_len)
    {
        size_t chunk_len = (total_len - offset > 64) ? 64 : (total_len - offset);

        // 循环直到成功发送这一段
        while (usb_vcp_send_data(&g_vars.usb_fs_handle->dev,
                                 (uint8_t*)(msg + offset),
                                 chunk_len) != SUCCESS);

        offset += chunk_len;
    }
}

/**
 * @brief 初始化命令解析器
 *
 * 注册所有支持的命令。
 */
void command_parser_init(void)
{
    command_table_init();
    // command_reader_init();
    register_command("lscmd", lscmd_handler);
    register_command("echo", echo_handler);
}

/**
 * @brief 命令解析任务
 *
 * 从命令 FIFO 中取出一条命令，调用命令表执行，并将结果通过 USB 返回。
 */
void command_parser_task(void)
{
    char buf[128];
    uint16_t len;

    if (command_reader_pop(buf, &len))
    {
        char *output = NULL;
        if (execute_command(buf, &output) == true)
        {
            usb_send_response(output);
        }
        else
        {
            usb_send_response(output); // 未知命令提示
        }
    }
}


