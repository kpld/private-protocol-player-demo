# 写 kdsp.c 之前的核对笔记（只讲集成点，不写核心实现）

> 写于 2026-11-09，依据本机实际源码树逐项核实，不是凭记忆：
> FFmpeg 树 `~/workspace/projects/ffmpeg-ijk-ref`（ijk 的 FFmpeg，
> ff3.4--ijk0.8.7--20180103--001）、ijkplayer k0.8.8（`~/workspace/projects/ijkplayer-src`）。
> 核心实现（帧头校验、magic 重同步、缓冲、断线处理）仍是你的手笔，
> 这份笔记只把“接进这棵树时容易踩的点”先排掉。

## 1. kdsp_open：建议走嵌套 tcp，而不是自己开 socket

这棵树里已有成熟的套路：`crypto.c:143`、`rtpproto.c:413`、`http.c:241`
都是在自己的 `url_open` 里用下面这个调用去开下层协议：

```c
ffurl_open_whitelist(&c->inner, "tcp://host:port", flags,
                     &h->interrupt_callback, NULL,
                     h->protocol_whitelist, h->protocol_blacklist, h);
```

好处：直接复用 ijk 的 tcp 实现（DNS 缓存、连接超时、
`interrupt_callback`、ijk 的 TCP 回调），不用在 kdsp.c 里重写
`getaddrinfo` / `ff_listen_connect` 那一整套（见 `tcp.c:341` 起的
`tcp_open`）。URL 解析用 `av_url_split`（`tcp.c:367` 的用法），
校验 scheme 是 `kdsp`，端口缺省 19350（见 `protocol.md`）。

注意：白名单要原样往下传（上面调用里的 `h->protocol_whitelist`）。
Demo App 里已经写好了（`MainActivity.java:80`）：
`protocol_whitelist = "kdsp,crypto,file,http,https,tcp,tls,udp"`，
`kdsp` 和 `tcp` 都在里面，嵌套打开不会被白名单挡住。

如果你坚持自己开 socket，参照物是 `tcp.c`：`ff_network_wait_fd_timeout`
做读写等待，出错返回 `ff_neterrno()`，成功后置 `h->is_streamed = 1`
（`tcp.c:505`）。

## 2. 一定要填 `.priv_data_size`（桩文件里漏了这一点）

构建树里的临时桩 `kdsp.c` 为了只验管线，没有定义私有上下文，
`ff_kdsp_protocol` 里也没有 `.priv_data_size`。真实文件必须补上，
否则 `h->priv_data` 是 NULL：

```c
typedef struct KDSPContext {
    URLContext *inner;      /* 嵌套的 tcp 连接（走方案 A 时） */
    /* + 帧缓冲 / 序号 / 计数器 / 起始时间，按你的实现加 */
} KDSPContext;

const URLProtocol ff_kdsp_protocol = {
    .name           = "kdsp",
    .url_open       = kdsp_open,
    .url_read       = kdsp_read,
    .url_seek       = kdsp_seek,
    .url_close      = kdsp_close,
    .priv_data_size = sizeof(KDSPContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
};
```

参照：`data_uri.c:117`、`.priv_data_size = sizeof(DataContext)`；
`tcp.c:853`、`.priv_data_size = sizeof(TCPContext)`。

## 3. kdsp_seek：直播流返回 ENOSYS 就够了

- `kdsp_seek` 对 `SEEK_SET / SEEK_CUR / SEEK_END` 返回
  `AVERROR(ENOSYS)` 即可；`AVSEEK_SIZE` 同样返回错误（流长度未知）。
- 不用担心这会让 avformat 以为流可 seek：`avio.c` 的 `ffurl_connect`
  里，打开后会试一次 `ffurl_seek(uc, 0, SEEK_SET)`，失败就自动置
  `uc->is_streamed = 1`。更稳妥的做法是在 `kdsp_open` 里直接置
  `h->is_streamed = 1`，和 `tcp.c`、`rtpproto.c:458` 保持一致。

## 4. kdsp_read：按“流式字节”而不是“按帧”来理解调用方

`url.h` 里 `url_read` 的约定：有数据（哪怕少于 `size`）、到 EOF
或出错就立即返回；非阻塞模式下无数据返回 `AVERROR(EAGAIN)`；
阻塞模式等一小会（0.1 s 量级）再返回 `EAGAIN`。上层的
`retry_transfer_wrapper`（`avio.c`）会循环调用直到凑够 `size`。

所以实现上要注意两点：

- **内部要有余量缓冲**：一次 `kdsp_read` 可能只被要走一帧的一部分，
  也可能一帧比 `size` 大。先把整帧读进私有缓冲，再按调用方要的
  长度分次给出去，不能假设“一次调用 = 一帧”。
- **只返回剥掉 22 字节帧头后的 Annex B 负载**，这样上层 H.264
  demuxer 的探测才能直接认出码流。帧头校验、magic 重同步、
  `payload_length > 8 MiB` 视为失同步（见 `protocol.md` 接收规则），
  都在读满一帧的内部步骤里做。
- 中断：循环读时检查 `h->interrupt_callback`，被中断返回
  `AVERROR_EXIT`（嵌套 tcp 方案里下层已经处理了这一层）。

## 5. kdsp_close 与两个速率函数

- `kdsp_close`：嵌套方案就是 `ffurl_close(c->inner)`（失败路径用
  `ffurl_closep(&c->inner)`），返回 0。声明见 `url.h:228-229`。
- `kdsp_average_speed / kdsp_realtime_speed`：只需要在读路径的
  计数器上挂钩——累计收到的负载字节数 + 首次收到数据时用
  `av_gettime_relative()`（`libavutil/time.h:38`）记起始时间。
  平均速率 = 累计字节 / 总耗时；实时速率 = 最近一个窗口内的字节 /
  窗口时长。窗口大小、是否用滑动窗口由你定，笔记里不替你定。

## 6. 写好后的验证步骤（你发我文件后我来跑）

1. 把真实 `kdsp.c` 替换构建树 `~/workspace/projects/ijkplayer-build/android/contrib/ffmpeg-arm64/libavformat/kdsp.c` 里的桩。
2. **先删 `ffmpeg-arm64/config.h` 再重编**（ijk 的 "reuse configure"
   逻辑，否则新协议进不了配置——2026-11-09 已实测踩过，见
   `docs/build-companion.md`）。
3. 验收：`config.h` 里 `CONFIG_KDSP_PROTOCOL 1`；重编后的
   `libijkffmpeg.so` 里有 `ff_kdsp_protocol` 符号（桩验证时已跑通）。
4. 真机联调：服务端 `server/kdsp_server.py` 起流，手机与服务端同局域网，
   App 里打开 `kdsp://<PC-LAN-IP>:19350`。
