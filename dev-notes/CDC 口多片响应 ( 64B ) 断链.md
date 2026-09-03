# 调试记录: CDC 口多片响应 ( >64B ) 断链

> 触发项目: DAQ13010 双 CDC 音频采集回播卡 (AT32F435, CherryUSB dwc2)
> 关联提交: `d41e7dc` fix: CDC 多片响应断链 + 分片发送改 XFRC 中断自驱动
> 日期: 2026-09-04

## 1. 现象

- 单片响应 ( ≤64B, 如 `stream` → `mic:off spk:off` ) 正常, 秒回;
- 多片响应 ( >64B, 如 `stream -h` 帮助 455B = 8 片 ) 异常:
  - 串口助手 ( 秒级轮询读 ) 场景: 首片后卡死数十秒, 迟到的内容与后续
    命令的响应交错污染 ( 帮助文本中混入 `mic:off spk:off` );
  - Python 持续读场景: 10s 零字节返回;
  - 上电后首个多片命令经常无响应, 之后的短命令也可能被连带丢弃。
- 与读取工具节奏无关: 串口助手与持续读脚本都复现。

## 2. 发送架构 ( 三层 )

```
parser (WAIT_CMD/SENDING)            ← 状态机
  └─ command_io_usb.c                 ← tx_idle / tx_busy / tx_rest / tx_buf
       └─ usb_cdc_send / XFRC ISR    ← ep_cdc_tx_busy ( 硬件传输忙 )
```

两组"忙"标志:

- 主循环层 `tx_busy`: 一片是否已发出 ( 由 XFRC ISR 清 );
- EP 层 `ep_cdc_tx_busy`: 硬件传输进行中 ( 由 XFRC ISR 清, 位于主循环层清理之前 )。

同一 XFRC ISR 内按序清理两者, 单核上原子。

## 3. 排查过程

1. **对比公开版框架**: DAQ 工程与公开版逐字节 diff, 仅 include 与注册命令不同
   → 排除本地改动引入。
2. **工具节奏假设**: 编写持续读脚本 ( 20ms 周期 read ), 多片仍 10s 无响应
   → 排除"主机不读"假设 ( 短命令秒回证明 IN 链路健康 )。
3. **长度梯度扫描陷阱**: 用 `echo + N字符` 制造不同长度响应, 得"≥64B 全断"。
   **假象**: echo 参数使命令本身超 `CMD_MAX_LENGTH=64`, 命令被拒, 与响应无关。
   教训: 扫描须用"命令短、响应长"的合法命令 ( device/lscmd/stream status/stream -h )。
4. **插桩定位** ( 计数器 + LCD 临时显示, 验证后已移除 ):
   - 每次长响应: send ok=2, XFRC=2, send fail=1, continue fail=1
   → 发送在第 3 片被 `ep_cdc_tx_busy` 拒绝后断链。

## 4. 根因

**主循环空转极快** ( 一轮可能 <1µs ), 而每片 XFRC ( 硬件完成 ) 到 ISR 清理
`ep_cdc_tx_busy` 之间存在**非零延迟窗口** ( dwc2 中断负载: FIFO empty + XFRC,
NVIC 响应延迟等 )。主循环在该窗口内轮询到发送状态机时:

```
usb_tx_continue → usb_cdc_send
    → 检查 ep_cdc_tx_busy == true  ( ISR 尚未清 )
    → 返回 false
```

旧代码失败分支:

```c
tx_idle = true;      /* 宣布整轮发送结束 */
tx_busy = false;     /* 只清主循环层 */
return false;        /* parser 回 WAIT_CMD */
```

- **EP 层 `ep_cdc_tx_busy` 保持 true** ( 那片其实已被主机收走, ISR 稍后才清 );
- parser 误判发送结束 → 响应截断;
- 后续新命令 `tx_start` 看到主循环层"空闲" → 尝试发送 → 又撞 EP 层残留 busy
  → 新响应也被丢弃 ( "上电后首命令无响应" );
- 迟到 XFRC ISR 清掉残留后, 滞留的半截链在主机下次读取时被吐出, 此时共享
  `resp` 缓冲已被后续命令覆写 → **内容交错污染** ( 帮助文本混入摘要 )。

关键点: 撞窗是**瞬时且必然**的 ( 主循环越快概率越高 ), 旧代码把它放大为
**一次失败即永久性拆毁发送状态机**。

## 5. 修复 ( 两层 )

1. **失败不放弃** ( usb_tx_continue ): 瞬时发送失败不再置 `tx_idle/tx_busy=false`
   放弃整条响应, 保持 SENDING 状态由 parser 下轮重试。
2. **XFRC 中断自驱动** ( usb_tx_handler ): 每片 XFRC 完成后在 ISR 内直接续发
   下一片 —— 此时 `ep_cdc_tx_busy` 刚被同一回调清 false, 发送必成功,
   **撞窗与重试从根本上消失**; 主循环不再逐片推进, `tx_continue` 退化为兜底。

## 6. 验证

- 455B ( `stream -h` ) 多片响应即时完整返回, 连续多次无异常;
- 插桩显示 continue fail = 0, 发送期间主循环占用下降;
- 短命令、2 片 ( device ) 回归正常。

## 7. 经验

- "主机读得慢"只是表象的放大器; 真正的缺陷在发送状态机对瞬时忙的处置。
- 状态机的失败路径必须可恢复: 瞬时错误 ( 等一帧即可消除 ) 应重试,
  结构性错误才应放弃。
- USB 分片发送的成熟形态是**传输完成中断自驱动**, 主循环只负责发起与等待收尾。
- 用命令造响应长度时注意命令本身受 `CMD_MAX_LENGTH` 限制。
