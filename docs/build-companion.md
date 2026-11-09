# 陪跑构建清单 — 核心 patch 的集成与构建（已核实版）

> 2026-11-09 由 Muse 在本机拉源码逐项核实，不是凭记忆写的。
> 参考源码位置（本机）：`~/workspace/projects/ijkplayer-src`（k0.8.8）、
> `~/workspace/projects/ffmpeg-ijk-ref`（ijk 的 FFmpeg，见下）。

## 已核实事实

1. **工具链（官方口径）**：ijkplayer k0.8.8 的 README 写明 Android 构建要求
   **NDK r10e**、Android Studio 2.1.3、Gradle 2.14.1。这是 2018 年的组合；
   实操中先按官方口径在 Docker 里冻结 r10e 试构建，踩不过再议换 NDK 并记录。
2. **FFmpeg 来源**：`init-android.sh` 从 `github.com/Bilibili/FFmpeg` 拉取，
   提交为 `ff3.4--ijk0.8.7--20180103--001`，base 放在 `extra/ffmpeg`，
   各架构副本放在 `android/contrib/ffmpeg-<arch>`（我们要的是 `ffmpeg-arm64`）。
   **patch 要打在 `android/contrib/ffmpeg-arm64` 这份上**（或 base 后重新 pull）。
3. **新协议的注册方式（这棵 FFmpeg 3.4 树已核实）**：
   - `libavformat/protocols.c`：加一行 `extern const URLProtocol ff_kdsp_protocol;`
     —— configure 用 `find_things_extern` 从这个文件自动生成协议列表，
     **不需要改 configure 的任何列表**；
   - `libavformat/Makefile`：加 `OBJS-$(CONFIG_KDSP_PROTOCOL) += kdsp.o`
     （参照同文件 `OBJS-$(CONFIG_TCP_PROTOCOL) += tcp.o`）；
   - `libavformat/kdsp.c`：新文件，实现 `ff_kdsp_protocol`
     （最小参照：同目录 `data_uri.c` 的 `ff_data_protocol` 结构；
     网络参照：`tcp.c`）；
   - 协议列表 `protocol_list.c` 由 configure 生成，不用手改。
4. **默认启用的**：`init-config.sh` 默认用 `config/module-lite.sh`，其中是
   `--enable-protocols`（全开）再逐个 `--disable-protocol=` 关掉不要的。
   所以 kdsp 协议**默认就会被编译进去**，除非被显式 disable；构建后用
   `grep CONFIG_KDSP_PROTOCOL android/contrib/build/ffmpeg-arm64/*/config.h`
   （路径以实际产物为准）验证为 1。
5. **另一个现成参照**：ijkplayer 本体在 `ijkmedia/ijkplayer/ijkavformat/`
   里自带一套自定义 IO 协议（`ijkioprotocol.c` 等）。如果 FFmpeg 层接入
   遇到阻力，可以参照它的做法在 ijk 层接入——但首选仍是 FFmpeg 协议层，
   卖点更硬。

## 分工与步骤

| 步 | 谁 | 做什么 | 完成标志 |
|---|---|---|---|
| 1 | Muse | 在 Docker/VM 里备好 NDK r10e + 源码树，跑通**原版无 patch** 的 arm64 构建 | 产出原版 libijkffmpeg.so / ijkplayer .so，构建日志存 `docs/` |
| 2 | Li | 写 `libavformat/kdsp.c`（按 protocol.md 的接收规则；可先在本机 PC 用这棵 FFmpeg 的 ffplay 目标自测逻辑） | kdsp.c 提交到本仓库 `player/ffmpeg-patch/` |
| 3 | Muse | 按上面第 3 点把 kdsp.c 集成进 ffmpeg-arm64 树、生成 patch 文件、重新构建 | config.h 里 CONFIG_KDSP_PROTOCOL=1 且构建通过 |
| 4 | 一起 | 安卓 App 连 `kdsp://` 打服务端流，排错（whitelist、demuxer 探测、硬解开关） | 手机上播出画面 + 录屏存 `docs/screenshots/` |
| 5 | Muse | 更新 README 实测数据、延迟测试表、发布前检查 | 仓库达到可公开状态，等 Li 定 GitHub 账号后推送 |

## 构建环境（2026-11-09 已就绪，Li 选了在 VM 上构建）

- JDK：Amazon Corretto OpenJDK 17.0.21，`~/opt/jdk-17`（`java`/`javac` 已验）
- NDK：r10e-rc4 (64-bit)，`~/opt/android-ndk-r10e`，已实测：`ndk-build` 可运行，
  aarch64-linux-android-4.9 的 gcc 能在本机跑并用 android-21/arm64 sysroot
  编出合法的 ARM aarch64 目标文件——老工具链在本 VM 上能用的最大未知数已排除。
- 磁盘：装完后 VM 仍有约 146 GB 可用。

## 基线构建结果（2026-11-09 ✅ 原版 arm64 全线跑通）

构建树：`~/workspace/projects/ijkplayer-build`（从参考克隆复制，参考克隆保持干净）。
日志：`docs/build-logs/`（init-android.log、compile-ffmpeg-arm64.log、compile-ijk-arm64.log，
三步退出码均为 0）。

- init：FFmpeg 检出在 2902e33f6e5（IJK_FFMPEG_COMMIT 的树顶），libyuv/soundtouch 就位
- FFmpeg arm64：六个静态库齐全（libavcodec.a 27 MB 等），
  config.h 中 HTTP/RTMP/TCP 协议均为 1（基线参照点）
- ijk 本体：三个 .so 均为 ELF 64-bit ARM aarch64 ——
  libijkffmpeg.so 3.58 MB、libijkplayer.so 381 KB、libijksdl.so 481 KB；
  libijkffmpeg.so 中验到了 ff_http_protocol / ff_tcp_protocol 符号
- 耗时参考（本 VM 共享 CPU）：init ≈9 分钟、FFmpeg ≈4.5 分钟、ijk 本体 ≈40 秒

**踩到的坑（已解决，复现构建时注意）**：NDK r10e 的
`make-standalone-toolchain.sh` 在本机把宿主误判为 linux-x86（32 位），
首次 compile-ffmpeg 直接失败。解法：按 ijk 期望的路径
`android/contrib/build/ffmpeg-arm64/toolchain` 手动预生成独立工具链，
显式加 `--system=linux-x86_64`，再 `touch <toolchain>/touch`，
ijk 脚本检测到标记文件就会跳过自己的工具链生成步骤。

## KDSP 集成管线验证（2026-11-09 ✅，用临时桩验证，未动核心实现）

Li 给定的函数面：`kdsp_open / kdsp_read / kdsp_seek / kdsp_close` 为 URLProtocol
四回调，另有 `kdsp_average_speed / kdsp_realtime_speed` 两个速率统计函数。
集成三步已在构建树 `android/contrib/ffmpeg-arm64` 落地并验证：

- `libavformat/protocols.c` 加了 `extern const URLProtocol ff_kdsp_protocol;`
- `libavformat/Makefile` 加了 `OBJS-$(CONFIG_KDSP_PROTOCOL) += kdsp.o`
- 临时桩 kdsp.c（仅构建树内，不进本仓库）重编结果：configure 自动生成
  `#define CONFIG_KDSP_PROTOCOL 1`；ijk 重编后 `libijkffmpeg.so`（3,585,240 B）
  中验到 `ff_kdsp_protocol` 符号与 "kdsp" 协议名字符串。日志：
  `docs/build-logs/compile-ffmpeg-arm64-kdsp-stub.log`、`compile-ijk-arm64-kdsp-stub.log`。

**坑（重要）**：ijk 的 do-compile-ffmpeg.sh 有 "reuse configure" 逻辑——源码目录
只要已有 config.h 就跳过 configure。新增协议后若不先删
`ffmpeg-arm64/config.h`，构建会 6 秒"成功"但新协议根本没编进去
（make 会警告 protocols.c newer than config.h）。加协议/组件后必须删 config.h 强制重配。

待办：真实 kdsp.c（四回调函数体 + 两个速率函数）替换桩文件后，
重复上述重编即可，然后从构建树导出 patch 到 `player/ffmpeg-patch/`。

## 当前 blockers

- **真机**：第 4 步需要一台 arm64 安卓手机连到服务端所在的局域网，
  这一步只能在 Li 的环境里完成。
