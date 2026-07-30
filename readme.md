# 类Linux CLI命令解析框架

## 概述
这是一个通用的嵌入式命令解析框架，支持在 **AT32** 平台上通过以下通信接口接收命令并解析执行：

- **RS485 (USART + DMA)**：[`USART 版本 IO 驱动`](command_io_usart.c)
- **USB CDC (虚拟串口)**：[`USB CDC 版本 IO 驱动`](command_io_usb_cdc.c)

两者均实现统一的接口，编译时按需选用其一。框架采用 **Linux CLI 风格**，支持命令注册、帮助信息、参数解析和统一反馈。

## 模块结构

### 1. command_io
- **职责**：从通信接口接收数据，拼接命令，检测 `\r\n` 结束符，并发送响应。
- **实现文件**：
  - [`command_io_usart.c`](command_io_usart.c) — RS485 (USART + DMA) 版，中断驱动
  - [`command_io_usb_cdc.c`](command_io_usb_cdc.c) — USB CDC 版，主循环轮询
- **接口**：
  - `command_io_init()`：初始化通信接口。
  - `command_io_pop()`：从命令 FIFO 取出完整命令。
  - `command_io_put_char()` / `command_io_put_string()` / `command_io_put_buf()`：向发送缓冲区压入数据。
  - `command_io_transmit_start()`：启动发送（非阻塞）。
  - `command_io_is_tx_busy()`：查询发送是否完成。
  - `command_io_flush()`：清空收发缓冲区。
  - `command_io_rx_handler()` / `command_io_tx_handler()`：接收 / 发送中断处理。

### 2. command_fifo
- **职责**：环形缓冲区，存储命令字符串。
- **接口**：
  - `command_fifo_init()`  
  - `command_fifo_push()`  
  - `command_fifo_pop()`  
  - `command_fifo_is_empty()`  
  - `command_fifo_is_full()`

### 3. command_table
- **职责**：命令注册与查找。
- **接口**：
  - `command_table_init()`  
  - `register_command(name, handler)`  
  - `execute_command(input, output)`  
  - `command_table_count()`  
  - `command_table_get_name(index)`

### 4. command_handler
- **职责**：集中存放命令实现。
- **默认实现命令**：
  - `lscmd`：列出所有命令。
  - `echo`：原样输出参数。

### 5. command_parser
- **职责**：调度命令执行与反馈。
- **接口**：
  - `command_parser_init()`：注册命令。
  - `command_parser_task()`：取命令并执行，通过通信接口反馈结果。

## 使用方法

1. **初始化阶段**：
   ```c
   command_io_init();
   command_parser_init();
   ```

2. **主循环**：
   ```c
   while (1) 
   {
       command_io_rx_handler();   // 接收数据（仅在使用USB CDC实现通信时需要此函数调用）
       command_parser_task();     // 取命令并执行
   }
   ```

3. **测试命令**：
   - 输入 `lscmd` → 列出所有命令。  
   - 输入 `echo hello world` → 输出 `hello world`。  
   - 输入 `lscmd -h` 或 `echo --help` → 显示帮助信息。  


## 如何新增命令

框架支持快速扩展命令，只需三步，下面以新增`newcmd`为例：

### 1. 在 `command_handler.c/h` 中实现命令处理函数
- 在 `command_handler.h` 声明函数：
  ```c
  char* newcmd_handler(int argc, char **argv);
  ```
- 在 `command_handler.c` 定义函数：
  ```c
  char* newcmd_handler(int argc, char **argv)
  {
      static char resp[128]; // 不建议单独分配反馈缓冲区占用空间，更推荐使用command_handler.c内已定义的全局变量

      // 帮助信息
      if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
      {
          snprintf(resp, sizeof(resp),
                   "Usage: newcmd [options]\r\n"
                   "Options:\r\n"
                   "  -h, --help    Show this help message\r\n"
                   "\r\n"
                   "Description of newcmd.\r\n");
          return resp;
      }

      // 默认行为
      snprintf(resp, sizeof(resp), "newcmd executed!\r\n");
      return resp;
  }
  ```

### 2. 在 `command_parser_init` 中注册命令
```c
void command_parser_init(void)
{
    command_table_init();
    register_command("lscmd", lscmd_handler);
    register_command("echo", echo_handler);
    register_command("newcmd", newcmd_handler); // 新增命令
}
```

### 3. 测试命令
- 输入 `newcmd` → 执行默认行为。  
- 输入 `newcmd -h` 或 `newcmd --help` → 显示帮助信息。  

### 注意事项
- 所有命令必须返回一个字符串指针（通常指向 `resp` 缓冲区）。  
- 建议统一支持 `-h` / `--help` 参数，保持 Linux CLI 风格。  
- 如果命令需要复杂参数解析，可以在 handler 内部扩展 `argc` / `argv` 的逻辑。  
- 推荐使用 `snprintf` 而不是 `sprintf`保证数据区域安全。  
