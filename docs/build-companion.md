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
   - `libavformat/protocols.c`：加一行 `extern const URLProtocol ff_kpld_protocol;`
     —— configure 用 `find_things_extern` 从这个文件自动生成协议列表，
     **不需要改 configure 的任何列表**；
   - `libavformat/Makefile`：加 `OBJS-$(CONFIG_KPLD_PROTOCOL) += kpld.o`
     （参照同文件 `OBJS-$(CONFIG_TCP_PROTOCOL) += tcp.o`）；
   - `libavformat/kpld.c`：新文件，实现 `ff_kpld_protocol`
     （最小参照：同目录 `data_uri.c` 的 `ff_data_protocol` 结构；
     网络参照：`tcp.c`）；
   - 协议列表 `protocol_list.c` 由 configure 生成，不用手改。
4. **默认启用的**：`init-config.sh` 默认用 `config/module-lite.sh`，其中是
   `--enable-protocols`（全开）再逐个 `--disable-protocol=` 关掉不要的。
   所以 kpld 协议**默认就会被编译进去**，除非被显式 disable；构建后用
   `grep CONFIG_KPLD_PROTOCOL android/contrib/build/ffmpeg-arm64/*/config.h`
   （路径以实际产物为准）验证为 1。
5. **另一个现成参照**：ijkplayer 本体在 `ijkmedia/ijkplayer/ijkavformat/`
   里自带一套自定义 IO 协议（`ijkioprotocol.c` 等）。如果 FFmpeg 层接入
   遇到阻力，可以参照它的做法在 ijk 层接入——但首选仍是 FFmpeg 协议层，
   卖点更硬。

## 分工与步骤

| 步 | 谁 | 做什么 | 完成标志 |
|---|---|---|---|
| 1 | Muse | 在 Docker/VM 里备好 NDK r10e + 源码树，跑通**原版无 patch** 的 arm64 构建 | 产出原版 libijkffmpeg.so / ijkplayer .so，构建日志存 `docs/` |
| 2 | Li | 写 `libavformat/kpld.c`（按 protocol.md 的接收规则；可先在本机 PC 用这棵 FFmpeg 的 ffplay 目标自测逻辑） | kpld.c 提交到本仓库 `player/ffmpeg-patch/` |
| 3 | Muse | 按上面第 3 点把 kpld.c 集成进 ffmpeg-arm64 树、生成 patch 文件、重新构建 | config.h 里 CONFIG_KPLD_PROTOCOL=1 且构建通过 |
| 4 | 一起 | 安卓 App 连 `kpld://` 打服务端流，排错（whitelist、demuxer 探测、硬解开关） | 手机上播出画面 + 录屏存 `docs/screenshots/` |
| 5 | Muse | 更新 README 实测数据、延迟测试表、发布前检查 | 仓库达到可公开状态，等 Li 定 GitHub 账号后推送 |

## 当前 blockers（开工前要定的）

- **构建在哪跑**：本 VM 目前没有 JDK/NDK/Docker。两个选项：
  (a) 经 Li 同意后在 VM 上装 JDK + NDK r10e（下载量约 1–2 GB 量级），
  Muse 全程陪跑；(b) 在 Li 自己的电脑上构建，Muse 远程陪排错。
- **真机**：第 4 步需要一台 arm64 安卓手机连到服务端所在的局域网，
  这一步只能在 Li 的环境里完成。
