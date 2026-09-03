#include "command_io.h"
#include "usb_cdc_uac.h"
#include <string.h>

static volatile bool tx_idle;                          /* 一轮发送是否空闲 */
static volatile bool tx_busy;                          /* 单块发送是否忙   */

static const uint8_t * volatile tx_buf;                /* 发送缓冲区       */
static volatile uint32_t tx_total_len;                 /* 发送总长度       */
static volatile uint32_t tx_rest;                      /* 发送剩余长度     */
static volatile bool require_send_zlp;                 /* 需要发送零长度包 */

static bool usb_init(void)
{
    tx_idle = true;
    tx_busy = false;
    return true;
}

static void usb_rx_buffer_flush(void)
{
    command_io_usb.rx_length = 0;
}

static void usb_rx_handler(const uint8_t *buf, uint32_t len)
{
    // 接口不可用则丢弃
    if (command_io_usb.available == false)
    {
        return;
    }

    // 检测剩余空间是否足够
    uint32_t space = RX_BUFFER_LENGTH_BYTE - command_io_usb.rx_length;
    if (len > space)
    {
        // 空间不足整体丢弃
        usb_rx_buffer_flush();
        return;
    }

    // 存入缓冲区
    memcpy(&command_io_usb.rx_buffer[command_io_usb.rx_length], buf, len);
    command_io_usb.rx_length += len;

    // 检查合法命令并提取
    command_io_fetch_cmd(&command_io_usb);
}

static bool usb_tx_start(const uint8_t *buf, uint32_t len)
{
    // 检测是否空闲
    if ((tx_idle == false) || (tx_busy == true))
    {
        return false;
    }

    // 开始发送
    tx_idle = false;
    tx_buf = buf;
    tx_total_len = len;
    tx_rest = tx_total_len;
    require_send_zlp = false;

    // 计算包长
    uint8_t chunk_len = (tx_rest > 64) ? 64 : tx_rest;
    if (((tx_rest - chunk_len) == 0) && (chunk_len == 64))
    {
        require_send_zlp = true;
    }

    // 开始发送
    tx_busy = true;
    if (usb_cdc_send(&tx_buf[tx_total_len - tx_rest], chunk_len) == true)
    {
        tx_rest -= chunk_len;
    }
    else
    {
        tx_idle = true;
        tx_busy = false;
        return false;
    }

    return true;
}

static void usb_tx_handler(void)
{
    /* XFRC 完成 (ISR 上下文): 上一片已被主机收走。
     *
     * 自驱动: 若仍有剩余内容, 直接在此续发下一片 —— ep_cdc_tx_busy
     * 已由外层 (usbd_cdc_acm_bulk_in_callback) 清 false, 本调用必成功,
     * 不存在主循环轮询撞 busy 的窗口。
     * 主循环不再参与逐片推进, usb_tx_continue 仅作兜底 (见下)。
     *
     * 失败兜底: 若 send 异常返回 false (理论上仅当 ep 层异常),
     * 保持 tx_busy=false 且 rest 不变, parser 将看到"空闲但未完成",
     * 转入 usb_tx_continue 重试。 */
    tx_busy = false;

    if (tx_rest > 0)
    {
        uint32_t chunk_len = (tx_rest > 64) ? 64 : tx_rest;
        if ((tx_rest - chunk_len) == 0 && chunk_len == 64)
        {
            require_send_zlp = true;
        }

        if (usb_cdc_send(&tx_buf[tx_total_len - tx_rest], chunk_len) == true)
        {
            tx_rest -= chunk_len;
            tx_busy = true;
        }
    }
    else if (require_send_zlp == true)
    {
        if (usb_cdc_send(NULL, 0) == true)
        {
            require_send_zlp = false;
            tx_busy = true;
        }
    }
    else
    {
        /* 全部内容 (含 ZLP) 发送完成 */
        tx_idle = true;
    }
}

static bool usb_tx_busy(void)
{
    return tx_busy;
}

static bool usb_tx_continue(void)
{
    // 单块发送忙时禁止继续发送
    if (tx_busy == true)
    {
        return false;
    }

    // 空闲状态无法继续发送
    if (tx_idle == true)
    {
        return false;
    }

    // 发送ZLP
    if (tx_rest == 0 && require_send_zlp == true)
    {
        tx_busy = true;

        if (usb_cdc_send(NULL, 0))
        {
            require_send_zlp = false;
        }
        /* 发送失败仅瞬时 (XFRC 完成与 ISR 清 busy 的窗口): 保持状态,
         * parser 下轮重试, 不放弃整条响应 */
        return true;
    }

    // 发送剩余内容
    uint32_t chunk_len = (tx_rest > 64) ? 64 : tx_rest;
    if ((tx_rest - chunk_len) == 0 && chunk_len == 64)
    {
        require_send_zlp = true;
    }

    if (usb_cdc_send(&tx_buf[tx_total_len - tx_rest], chunk_len) == true)
    {
        tx_rest -= chunk_len;
    }
    /* 发送失败仅瞬时 (XFRC 完成与 ISR 清 busy 的窗口): 保持状态,
     * parser 下轮重试, 不放弃整条响应 */

    return true;
}

static bool usb_tx_idle(void)
{
    return tx_idle;
}

command_io_t command_io_usb = 
{
    .available       = false,
    .rx_length       = 0,
    .init            = usb_init,
    .rx_buffer_flush = usb_rx_buffer_flush,
    .rx_handler      = usb_rx_handler,
    .tx_start        = usb_tx_start,
    .tx_handler      = usb_tx_handler,
    .tx_busy         = usb_tx_busy,
    .tx_continue     = usb_tx_continue,
    .tx_idle         = usb_tx_idle,
};
