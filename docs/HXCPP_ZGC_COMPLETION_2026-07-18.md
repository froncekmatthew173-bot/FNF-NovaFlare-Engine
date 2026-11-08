# NovaGC / hxcpp ZGC 完成记录（2026-07-18）

## 结论与边界

当前 `hxcpp/` 已实现适用于 hxcpp 精确对象模型的 ZGC 等价能力：单一托管堆、分区与分代、并发标记、并发 Young、并发重定位、彩色引用与自愈屏障、精确 remembered set、引用处理、故障恢复、内存策略和自适应调度均已落到正式运行时与生成代码路径中，不依赖旧 hxcpp GC、Immix 或 LegacyBridge。

NovaGC 不是 HotSpot 的源码移植，也不会伪装成 JVM。JFR、Java Reference API、HotSpot 命令行参数等 Java 专属接口不适用于 hxcpp；对应的可观测性由原生 JSONL/CSV 遥测、hxcpp handle/reference queue 和环境变量配置提供。这里的“完整”指 ZGC 的 GC 语义和工程能力在 hxcpp 对象 ABI 下完整实现，而不是逐字节复制 OpenJDK 内部结构。

## 已完成能力

### 精确对象图与屏障

- 64 位彩色 `Ref`、8 字节原子 `RefSlot`、稳定对象 identity 和 forwarding chain。
- 生成器发出的精确 `TypeDescriptor`、实例/静态根、`RootFrame`、自定义可变长对象 trace。
- load barrier 自愈，SATB overwrite、insertion/update、relocation forwarding 和 owner-less store barrier。
- Strong、Weak、Soft、Phantom、Pinned handle；generation-safe reference queue、ephemeron、WeakMap、finalizer 与 resurrection。

### 分代与并发收集

- Eden、Survivor、Old、Pinned、Large、YoungLarge 和 Relocation 空间。
- 独立 Young/Full mark bitmap 与 epoch；对象年龄、Survivor cohort、固定/自适应 tenuring 和 promotion reserve。
- 双缓冲精确 remembered bitmap、thread-local remembered buffer 和 act-once 处理；metadata OOM/队列溢出时保守保留并原地提升受影响 cohort，不扫描已死亡 Old 对象。
- concurrent Young 使用不可变 source cohort、initial/final pause、共享 outstanding-work queue、多 worker tracing/copy 和屏障协作；并发期新分配边可在 final remap 中正确愈合。
- concurrent Full mark、并行 remark work stealing 和多 worker relocation；Young/Full 调度可交互而不会混用 liveness epoch。

### 重定位与内存管理

- 按 region 的并发 evacuation、转发表、引用槽自愈、late pin 和动态 pinned-object 隔离。
- evacuation reserve 不足时支持无额外堆空间的 in-place relocation，并使用外部 forwarding map 保持解析正确。
- relocation allocation failure、Young promotion failure、半复制防护和 allocation-stall service 均有显式降级/恢复协议。
- 全局虚拟地址预留、按需 commit、延迟 uncommit、soft max、reservation cache、Large/Humongous 精确尺寸复用。
- Windows 与 POSIX 原生虚拟内存后端、NUMA placement、large/huge page 策略及安全回退。

### 调度、OOM 与可观测性

- 自动 Young/Full 触发，按 pause p99、吞吐、Survivor 压力、promotion debt 和 allocation stall 反馈调整 worker、tenuring 与 headroom。
- 普通 allocation retry、Soft 引用清理、Young、Full、emergency collection、最终 retry 的完整 OOM 顺序。
- cooperative epoch safepoint、native-blocking attach、慢握手/停顿诊断。
- JSONL/CSV 结构化周期遥测；支持触发阈值、暂停目标和日志路径环境配置。

## 本轮高推理复审发现并修复的问题

1. concurrent Young 的 source Eden 曾仍可被 TLAB 分配，且并发期新建 owner 指向 source 的边未被 final remap 覆盖。现在 source cohort 会立即冻结，final remap 会愈合所有存活非 evacuation 对象中的精确引用槽。
2. 动态 pin 隔离曾对无关字段调用通用 load barrier，可能沿已死亡对象图读取悬空边。现在 pin remapper 只替换精确等于被移动 source 的引用，并跳过 retired/forwarded/已知死亡对象。
3. Full 之后 mixed-live Old region 中的已死亡对象曾被 Young final remap 扫描。现在 final remap 只访问根、handle/reference 结构、remembered slots、survivor/promotion 目标、新 Eden 和 pinned source live set，不再遍历整块 Old region；死对象字段不会被 trace。
4. 自动 GC、emergency GC 和显式 GC 请求曾可能同时进入不同 phase。现在所有 Young、bootstrap/full mark、compact 和 relocation 入口都经过同一个 safepoint-cooperative phase transition mutex，并有 RAII phase scope；两个并发 Young 请求也有序列化回归。
5. remembered metadata 发布失败时，旧回退会扫描整块 Old region，既可能触碰死对象，又不能可靠恢复丢失边。现在故障注入路径会保守保留完整 source cohort、原地提升 fresh Eden，并跳过不安全的 heap-wide remap/rebuild trace；回归同时验证 256 条活边存活和不可达 Old 对象绝不被 trace。
6. 活跃 GC 期间的分配失败曾直接抛出 `bad_alloc`。现在 mutator 会进入有界、可协作 safepoint 的 allocation stall，累计 stall 遥测并重试；超时后才继续完整 emergency/OOM 序列。
7. concurrent Young 持有 phase 时创建 Pinned handle 曾可能与动态隔离重叠。现在 pin isolation 也参与 phase 序列化，并在等待前用临时精确根保护原始对象，使正在进行的 Young 能先愈合该引用。
8. 已确认 parked 的线程若携带旧 snapshot epoch，现在会在握手内重新捕获；未停驻却报告旧 epoch 的线程仍作为协议错误拒绝。
9. 真实 NF 高频 Full 在第 16 周期捕获到 emergency allocation 与自动 Full 在 `startConcurrentMark`/`finishConcurrentMark` 两段之间重入。现在 split Full 和 relocation 从 start 到 finish 持有运行时级 cycle ownership；等待线程持续执行 safepoint poll，不会抛出 `concurrent mark cycle is already active`，并有两个并发 Full 请求的独立序列化回归。
10. 8 GiB 虚拟地址预留曾被默认的 4096 个逻辑 region 元数据对象提前截断，真实 NF 在约第 29--31 个 Young 周期才会耗尽逻辑 region。现在 `maximumRegions` 只作为显式可选策略限制，默认硬容量完全由预留字节数约束；压力实跑已经创建并同时管理超过两万个 active region。
11. 真实 NF 的 Prime/OpenAL 外部调用曾被当作活跃 Haxe mutator，导致 GC safepoint 等待卡在 `lime_al_gen_buffer`。现在 Prime/CFFI 外部调用自动进入可嵌套 native-blocking，Haxe callback 会临时挂起全部 CFFI-owned blocking depth 后安全重入；嵌套 scope、并发收集和 callback re-entry 都有回归，公开 hxcpp/CFFI ABI 不变。
12. Lime finalizer 在持有 OpenAL 手工互斥锁时创建临时 Pinned handle，过去会触发递归 GC cycle；异常越过 Lime 的手工解锁后造成 `al_gc_mutex` 永久死锁。现在 owning GC cycle 内的 pin 会保留同一精确对象并计数固定，后续阶段排除其 region；finalizer 异常同时带类型与原因写入诊断，回归覆盖 finalizer 内创建、解析和释放 Pinned handle。

这些路径都有独立回归，并同时经过真实 Haxe 单元程序集和游戏发布构建覆盖。

## 验证门禁

- Windows GCC 16 CMake/CTest：39/39。
- Linux Debian GCC 12 全新容器 CMake/CTest：39/39。
- Haxe 生成单元程序集：11025/11025 assertions，0 error，0 failure；最终代码额外连续运行 8 次全部通过。
- Haxe 生成 probe matrix：9/9（array、closure、enum、event loop、exception、dynamic interface、Lime CFFI、Prime、thread）。
- Android NDK r29、arm64、API 24：`runtime.cpp` 在 `-Wall -Wextra -Wpedantic -Werror` 下编译通过。
- precise ABI、no-legacy audit 和 generated Haxe smoke：全部通过。
- NovaFlare Windows release：见下方真实游戏验证记录。

## 真实 NovaFlare 验证

验证对象为本轮完整源图生成和 2763 对象链接后生成的 `export/release/windows/bin/NovaFlare Engine.exe`，不是历史 EXE。最终二进制时间戳为 2026-07-18 17:59:37、大小为 1857644455 字节，SHA-256 为 `6AD276BB5C442882DEA75D086D6D591D52A616061DEB3F1258A82BB41E3E10F6`；构建同时通过 generated precise ABI 检查。

- 2026-07-18 18:00:56 启动可见 NF 窗口，进程 PID 17828；已进入并持续渲染完整标题界面，窗口标题为 `NovaFlare Engine`，最终检查时 `Responding=True` 且两秒采样间 CPU 时间继续增长。测试完成后未终止该实例。
- 测试启用了 `HXCPP_NOVAGC_VERIFY_REFERENCES=1`、`HXCPP_NOVAGC_VERIFY_REMEMBERED=1`，并把 initial/Young 阈值压到 16 MiB、Full 频率提高到每 2 次 Young 一次。
- 已连续完成 32 次 Young 和 16 次 Full，跨过旧实现约第 29--31 周期耗尽逻辑 region 的故障区间后又完成一轮。累计分配 590.6 MiB，峰值 created/active region 为 23595/23583，而 8 GiB arena 峰值仅 commit 434.9 MiB；证明默认 4096 只参与容量换算，不再限制逻辑 region 数量。
- 全程没有 safepoint timeout、cycle 重入、递归 GC、引用/remembered-set verifier 报错或 finalizer failure；`emergency_collections=0`、`allocation_stalls=0`、`allocation_stall_timeouts=0`。
- JSONL 的 48 行全部可解析，CSV 也有 48 行数据且逐周期数量一致；记录位于 `_build/nf-absolute-final/novagc.jsonl` 与 `_build/nf-absolute-final/novagc.csv`。
- 最终发布二进制在第 32 周期后的标题界面截图位于 `_build/nf-absolute-final/title-cycle32.png`。

该实跑故意使用 verifier、16 MiB 小阈值和高频 Full；在约 2.4 万个细粒度逻辑 region 上，观察到的最大 pause p99 为 1.638 秒。这是边界验证压力值，不是默认配置或生产帧延迟基线，也不据此宣称 HotSpot/JVM 的暂停指标。
