# hxcpp-zgc 重新审计（2026-07-18）

本文是高推理强度下对当前工作区的重新审计。2026-07-13 及更早的状态文档只作为历史记录；其中“单 worker”“没有 nursery/remembered set”“没有虚拟地址 reserve/commit”等结论已经过期。

## 审计基准

当前 OpenJDK ZGC 的基准采用：

- [JEP 439: Generational ZGC](https://openjdk.org/jeps/439)：分代、colored pointer、load/store barrier、SATB、双缓冲精确 remembered set、无需额外堆空间的 relocation。
- [JEP 376: Concurrent Thread-Stack Processing](https://openjdk.org/jeps/376)：线程栈处理移出 stop-the-world 阶段。
- [JEP 351: ZGC: Uncommit Unused Memory](https://openjdk.org/jeps/351)：延迟 uncommit 与物理内存归还。
- [Oracle JDK 25 ZGC 指南](https://docs.oracle.com/en/java/javase/25/gctuning/z-garbage-collector.html)：JDK 24 起只保留分代 ZGC，并包含动态 generation/worker/tenuring 调节。

NovaFlare 的运行时不是 HotSpot，也不能因为机制名称相似就宣称是完整 OpenJDK ZGC。以下只记录代码和可执行测试能证明的能力。

## 当前已经落地并有测试的能力

### 精确对象图与屏障

- 64 位带 view 位的 `Ref`、8 字节原子 `RefSlot`、精确 `TypeDescriptor`。
- 显式 `RootFrame/RootScope`、精确静态根、Strong/Weak/Soft/Phantom/Pinned handle 和 generation-safe reference queue。
- load barrier 解析 forwarding chain，并用当前 good view 自愈引用槽。
- concurrent full mark 使用 SATB overwrite barrier、insertion barrier 和新分配对象 born-live 协议。
- 字段写屏障覆盖显式 owner 与生成代码常见的 owner-less `ObjectPtr/Dynamic` 赋值。

### 分代基础

- 普通 TLAB 分配进入 Eden；Region 明确区分 Eden、Survivor、Old、Pinned、Large 和 Relocation 用途。
- young collection 只从精确根、pin、非 weak handle 和 old-to-young remembered slots 追踪 young 对象。
- remembered set 记录精确字段地址与 owner；活动表和 collection snapshot 双缓冲交换，mutator 先写 thread-local buffer，元数据 OOM 时以 per-region overflow 精确扫描恢复。
- live young 对象按年龄进入 Survivor 或晋升 Old；tenuring threshold 会按 Survivor 压力自适应，promotion reserve 不足时完整 source 原地晋升。
- old-to-survivor 边、relocation 新产生的跨代边、Weak/Soft/Phantom、ephemeron/finalizer 均有回归测试。
- 自动 GC 先进行 young cycle，按 promotion debt/young cycle 数触发 full cycle，并按暂停目标、Survivor 比率和 soft-max 压力调整 young headroom。

### 并发与内存

- concurrent full mark 支持多个共享队列 worker。
- concurrent relocation 支持多个按 source-region 分工的 worker。
- young trace 支持多个共享 outstanding-work queue worker；full mark 与 relocation 也支持多 worker。
- region 使用 Windows `MEM_RESERVE/MEM_COMMIT` 或 POSIX `mmap/mprotect` 按页提交；空 Region 进入按尺寸 best-fit 的 reservation cache，超过延迟后 `MEM_DECOMMIT`/`madvise` 归还物理页。
- Large/Humongous Region 使用精确尺寸 reservation，并可复用缓存的大块虚拟地址；Pinned 类型进入专用空间且不参加 relocation。
- 普通分配 OOM 会串行执行 retry、Soft 清理、Young、Full 和最终 retry；Young 与 concurrent relocation 均有不暴露半复制图的降级协议。
- 有 cooperative epoch safepoint、native-blocking attach 协议、暂停时长和慢握手诊断。

### 本轮重新执行新增/修正

1. 修复 concurrent relocation 中 owner-less 字段写丢失：屏障现在会在 heap/relocation 同步域内从 slot 地址反查 owner；若 source 已转发，则按字段 offset 写入新副本。
2. 修复 source 复制前的写入竞态：未转发 owner 的字段写在持有 relocation lock 时完成，保证 worker 随后的 `memcpy` 能看到写入。
3. 增加 relocation allocation failure 降级协议：部分复制的 source 保留，已完成 source 可正常退休，forwarding 保持可解析，周期会退出 `relocationActive`，对象图和后续 full mark 仍可用。
4. 新增确定性 copy gate、stale-source owner-less write、部分 evacuation 后 relocation OOM，以及虚拟地址 reserve/按需 commit/release 测试。
5. 分配失败会把尚未发布 forwarding 的当前对象从 `ObjectRelocating` 恢复到 `ObjectStable`，不把中间态泄漏给后续周期。
6. 补齐生成代码所需的 `TCast(HxNullValue)` 精确重载，消除 `TCast(null())` 在 `Dynamic`、UTF-8/UTF-16 字符指针重载之间的歧义，并增加独立兼容回归。
7. 实现真正 Survivor cohort、对象年龄、固定/自适应 tenuring、old-to-survivor remembered edge 持续化和 promotion failure 原地整区降级。
8. 实现 Soft/Phantom 处理与 reference queue、紧急 OOM 状态机、reservation cache、延迟 uncommit、Large/Pinned 专用空间。
9. 实现 thread-local remembered buffer、故障注入和 overflow Region 精确重扫；GC 内部 remembered 元数据失败同样降级，不跨越已完成 forwarding 抛异常。
10. Young 疏散在每个 source 建立 forwarding 前，为每个 live object 固化目标 Region 并提交完整高水位；OS commit 失败只能整区原地晋升，不能留下半复制图。

## 仍不能宣称为完整 ZGC 的部分

### 低暂停边界

- young tracing、evacuation、root remap 当前仍在一次 stop-the-world pause 内，工作量随 young live set 增长。
- full remark 仍会在 pause 内串行 drain tail graph、处理 weak/ephemeron/finalizer 和扫描 region 元数据。
- 根使用显式 RootFrame；没有机器级 stack/register map，也没有 JEP 376 风格并发线程栈处理。

### 分代完整性

- 已有 Eden/Survivor/Old、对象年龄、自适应 tenuring 和 young headroom，但调节器仍是本运行时的启发式实现，不等同 HotSpot 的完整 generation sizing/tenuring 策略。
- 没有 young/old 两套独立 colored marking/relocation metadata。
- large object 当前直接 old，不具备 Generational ZGC 的 young large-object 策略。
- remembered set 是 thread-local buffer 加精确 `unordered_map`/overflow Region 扫描，不是 ZGC 的 per-region 双 bitmap 和 act-once colored-field barrier。
- full collection 不是两个可独立并发、可交互的 young/old collector。

### 调度、空间与故障恢复

- worker 数会按配置、CPU 和 live work 选择；没有完整运行期反馈缩放、work stealing、NUMA/拓扑感知或并行 remark。
- relocation 仍需要额外 destination region；没有 ZGC 的“释放一个 source 后立即复用为 destination”无额外堆空间算法。
- 已有普通 allocation OOM、promotion failure、emergency collection、Soft 清理和串行恢复；尚没有像 HotSpot 那样完整的 allocation-stall 队列、超时诊断和服务等级策略。
- 已有 soft max、reservation cache 与延迟 uncommit；没有全堆统一虚拟地址预留、NUMA placement 或 huge pages。
- TypePinned 有独立 Pinned Region；运行期对普通对象的临时 native pin 仍会排除其所在的整个 Region。

### 引用与平台

- Weak/Soft/Phantom、generation-safe reference queue、ephemeron、WeakMap、finalizer/resurrection 已实现。
- Windows 是当前验证主路径；Linux/Android/macOS/iOS 未达到同等门禁。
- 文本 telemetry 已有，但没有 JFR 等价事件、JSON/CSV/Tracy 完整流水线和自动暂停分位数门禁。

## 本轮门禁

- CMake/GCC 16 内核测试：33/33。
- concurrent relocation + relocation failure：各重复 30 次，60/60。
- no-legacy 审计：通过。
- generated Haxe smoke：通过。
- generated Haxe unit：2161/2161 个 C++ 单元编译链接通过；运行 11025/11025 断言，0 错误、0 失败。
- generated Haxe probe matrix：9/9 通过（array、closure、enum、event loop、exception、dynamic interface、Lime CFFI、prime、thread）。
- Windows 游戏 release 构建：2763 个生成翻译单元中的 2510 个完成本轮编译，2763 个对象链接成功，NovaGC precise ABI verification 通过。

## 准确结论

当前实现是“单堆、精确、region/TLAB、带 colored view、并发 full mark、并发 relocation、Eden/Survivor/Old 分代、自适应 tenuring、精确 remembered set、专用 Pinned/Large 空间、引用队列、紧急 OOM 和延迟 uncommit 的 hxcpp 替代运行时”。这些能力已经落到代码、独立故障注入测试和完整 Windows 游戏链接中，不再只是路线或审计条目。

它仍不能等同于 HotSpot 的完整 Generational ZGC：Young tracing/evacuation/remap 仍是 stop-the-world，显式 `RootFrame` 尚未升级为机器 stack/register map 和并发线程栈处理，young/old 也不是两套可独立并发交互的 collector；此外仍缺无额外堆空间 relocation、NUMA/huge-page 策略及非 Windows 同等级门禁。后续若使用“完整 ZGC”一词，必须同时补齐这些架构项，不能仅凭已有机制名称宣称完成。
