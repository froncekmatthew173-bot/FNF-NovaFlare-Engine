# NovaFlare ZGC 性能优化：目标、状态、证据与阶段结论

> 项目：FNF-NovaFlare-Engine  
> 首建日期：2026-07-23  
> 最新更新：2026-07-26  
> 当前主任务：只优化 ZGC 的帧数、帧稳定性、GC/屏障开销和内存表现  
> 明确不在范围内：Immix 后续修改、Freeplay/BPM 显示问题

## 1. 总目标

本阶段需要同时解决和验证以下问题：

1. 找出 ZGC 相比此前运行表现帧数不稳、持续开销较高、平均帧数不高的真实原因。
2. 修复会放大 GC 长尾、堆积回收债务或破坏分代引用正确性的缺陷。
3. 降低 mutator 高频引用屏障的持续 CPU 开销。
4. 区分 GC 停顿、GC 并发竞争、线程调度、图形驱动和 OpenFL 渲染遍历造成的长帧。
5. 用同场景、同采集契约、同统计口径做修改前后对比，不用主观体感替代证据。
6. 强制抓取 Haxe 日志、异常栈、原生线程栈、堆/GC、帧率、进程资源、ETW 和 Windows 事件。
7. 每项有风险的修改必须有恢复点，构建尽量增量，临时大文件在提取证据后清理。
8. 对照 OpenJDK Generational ZGC 的设计，识别本项目还缺少的成熟优化，但不机械照抄 JVM 参数。

## 2. 当前总体状态

- 主任务保持不变：只优化 ZGC，不继续 Immix，不处理 Freeplay/BPM。
- Full→Young 引用正确性、恢复阈值、`RefSlot` 高频槽、
  `storeBarrier` 冷路径和 `PinnedBytes` 局部固定税修复均已通过回归并保留。
- 当前 NovaGC kernel 测试为 `40/40`；最新游戏构建只重新编译
  `runtime_facade.cpp`，为 `1/2774`，没有再次全量编译。
- 默认 512 MiB SoftMax 造成的 Full 风暴已经定位并修复：同窗口 Full
  `9→1`，修复后 230 秒内 `soft_max` Full 为 `0`。
- SoftMax 已从“越线即 Full”的硬开关改为同时受分配率和 Major 增长约束的
  压力信号，方向与 JDK 26 `ZDirector` 一致。
- 修复后正式轮通过 `8/8` 采集契约；Haxe/native/Windows 错误、
  stall、timeout、emergency、fallback 和 dump 全为 `0`。
- cadence cycle 136 的 69.55 MiB Major 增长曾触发 207.915 ms concurrent
  工作；默认最小 Major 增长现已由 64 提高到 128 MiB，正式复测中 cycle
  136～151 均停在 `cycle_growth_wait`，自动 Full 为 0。
- 精确 ETW 证明该 207.915 ms 不是同长度 STW：主线程最大离核间隙只有
  12.135 ms，真正机制是两个 Major worker 与满载游戏主线程争抢 CPU/缓存。
- 全局 worker 2→1 筛选实验没有覆盖 cadence Major，且平均吞吐/P99 没有
  改善，已否决为默认方案；后续只能做 Major 专用动态 worker。
- 128 MiB 第一轮曾出现 100～628 ms 驱动长帧；Haxe/ETW 定位到
  `Stage.render`、`nvoglv64.dll`、DXGI/dxgkrnl 和全进程离核。第二次同
  EXE 正式轮未重现，update/draw max 为 43/40 ms，不算作 GC 回退。
- 330 秒轮约 41.1 秒 ResultsScreen 图表构建卡顿已由 Haxe 栈独立归因，
  同期没有 Full，不计入 ZGC 修复收益。
- 当前不能宣称平均 FPS、P99 帧稳定性或“ZGC 已追平其他 GC”最终完成。
- 最新完整目标维度、状态和结论见 `17.6`；长期计划固定放在全文最后。

## 3. 第一、二阶段目标维度历史快照

| 目标维度 | 当前状态 | 证据 | 阶段结论 |
|---|---|---|---|
| ZGC 分代引用正确性 | 已修复并回归 | 已修复 Full→Young 悬空引用风险；GC 测试通过 | 正确性问题必须优先于性能调参，否则较好的帧数数据没有可信度 |
| 暂停后自动恢复阈值 | 已修复并验证 | collector 发布当前 Young 自适应 headroom；恢复时不再错误使用 Full 最大 256 MiB headroom | 修复了长期积累 Young 回收债务后一次性高成本回收的根因 |
| GC 长暂停 | 第一阶段已改善 | 全部 GC 最大值 `58.212→38.729 ms`；排除首次 GC 后最大值为 `14.469 ms` | 自动调度修复确实压低了常态 GC 长尾 |
| GC 调度稳定性 | 第一阶段已改善 | 最大 GC 间隔 `50.222→7.359 s` | Young GC 不再出现约 50 秒的异常断层 |
| Allocation stall / emergency GC | 已验证未出现 | 180 秒诊断无 allocation stall、无 emergency | 当前堆容量和新调度策略没有把问题转化成分配阻塞 |
| Update 帧稳定性 | 第二阶段同场景已复测 | 第一阶段→`RefSlot`：均值 `235.635→236.079`；P95 `21→23 ms`；P99 `32→33 ms`；max `208→34 ms` | 均值持平、极端尖峰消失，但 P95/P99 小幅波动，仍不能宣布全面解决 |
| Draw 帧稳定性 | 第二阶段同场景已复测 | 均值 `483.929→438.016`；P95 `20→25 ms`；P99 `30→33 ms`；max `206→36 ms` | 极端尖峰消失，但稳态 draw 吞吐回退约 `9.5%`；需按渲染/驱动路径继续处理 |
| 极端 `208/206 ms` 长帧 | 已完成归因 | 对齐绝对时间轴后，帧窗口内没有 GC；最近 Young GC 早约 112 ms 完成 | 该尖峰不能算作 ZGC 停顿 |
| OpenFL 渲染树开销 | 已确认热点 | ETW 栈主要为 `OpenGLRenderer.__renderDisplayObject`、`Context3DDisplayObjectContainer.renderDrawable`、`Stage.__render` | 后续需要与 ZGC 优化分开评估，避免把渲染瓶颈误判为 GC |
| 图形驱动/线程失速 | 已排除为该尖峰主因 | 主线程最大无采样间隙约 `5.283 ms`，驱动约 `5.695 ms` | `208/206 ms` 不符合单次驱动或线程调度阻塞的证据特征 |
| ZGC load barrier 持续开销 | 第二阶段已优化并验证 | 同场景主线程 `RefSlot/loadBarrier/storeBarrier` 直接叶采样 `5260→2857`，占比 `3.515%→1.894%` | 直接屏障叶开销约下降 `46%`，独立 `RefSlot::load` 热点已消失 |
| ZGC store barrier | 已审计，暂不盲改 | 已有 per-thread remembered buffer、容量预留和批量 flush；owner-less root 有 arena 快速排除 | 不能错误地以“缺少 store buffer”为理由重写该路径 |
| `RefSlot` 强制内联 | 已实现、验证并保留 | `load/store/exchange/CAS` 使用窄范围编译器 always-inline；真实游戏 defined symbol `0`；同场景 ETW 直接屏障叶采样下降 | 局部收益明确，未修改原子类型、memory order 或 collector 语义 |
| `ObjectPtr::get` / `Dynamic::Cast` | 暂不继续扩展内联 | 这些上层函数可能包含完整 barrier 序列 | 强制内联范围过大可能造成代码膨胀和指令缓存回退，因此本轮保持单变量 |
| hxcpp 目标测试 | 已通过 | `gc_automatic` 目标构建与测试通过 | 新优化没有破坏自动 GC 行为 |
| hxcpp 完整测试 | 已通过 | 最终 `39/39`，耗时约 `1.61 s` | 第一轮 `-j12` 的 thread-shutdown 是 5 秒边界假超时；单测与 `-j4` 全套均通过 |
| 实际游戏构建 | 已完成 | 修复 PCH 依赖后明确打印 `rebuilding precompiled runtime header`，并完成 `2774/2774` 编译 | 公共 ABI 头必须触发一次全量构建；PCH 依赖修复避免今后错误复用旧 ABI |
| 实际 EXE 机器码/符号 | 已核验 | 正式 EXE SHA-256 `FEE1AB...EC14`；旁路游戏链接中 `RefSlot::load` defined symbol 为 `0` | 真实游戏对象已应用内联，不再只是单测二进制结论 |
| CPU 总开销 | 第二阶段同场景小幅改善 | 第一阶段 Title→`RefSlot` Title：`19.007%→18.728%`，下降 `0.279` 个百分点 | 局部屏障收益已反映到总 CPU，但幅度较小，仍需多轮确认 |
| Private Bytes | 第二阶段继续改善 | `577.642→560.887 MiB`，下降 `16.755 MiB` | 同场景下没有因强制内联产生堆或进程内存膨胀 |
| Working Set | 第二阶段小幅回退 | `437.063→445.273 MiB`，增加 `8.210 MiB` | 与 Private Bytes、committed heap 的下降方向不同，暂按 OS/图形驻留波动处理 |
| Haxe 日志 | 已纳入强制采集 | 上一轮完整抓取且无 Haxe 崩溃 | 本轮仍作为 `7/7` 契约的必需项 |
| Haxe 异常栈 | 已纳入强制采集 | CrashHandler/异常输出路径被保留；上一轮没有异常 | “没有报错”必须由日志文件和退出状态共同证明 |
| 原生线程栈 | 第二阶段对比完成 | 新旧 ETW 总采样约 28.5 万；主线程占比 `52.645%→52.817%`，渲染线程 `44.359%→44.182%` | 线程 CPU 分布基本不变；`RefSlot::load` 独立叶节点归零 |
| 堆/GC 数据 | 第二阶段对比完成 | 同场景 39→38 条 GC；分配率 `4.146→3.925 MiB/s`；稳态 committed `254.052→238.326 MiB` | `RefSlot` 阶段未增加分配和堆压力，稳态 GC 平均/P95/max 均改善 |
| 帧日志时间轴 | 已增强并验证 | Haxe perf trace 新增绝对 `wall_time_ms`；转换脚本兼容旧格式 | 解决了帧尖峰与 ETW/GC 只能模糊对齐的问题 |
| 进程资源 | 已纳入持续监视 | 上一轮 616 条 process 样本 | CPU、Private Bytes、Working Set 会继续按相同口径比较 |
| Windows 事件 | 已纳入契约 | 上一轮无 Application Error、无挂起记录 | 可排除静默崩溃或被监测脚本误判的退出 |
| dump/崩溃文件 | 已纳入契约 | 上一轮无 dump | 本轮如生成 dump，必须同时保存异常栈和 ETW 邻近窗口 |
| JDK ZGC 对比 | 已完成架构级对照 | 对照 JEP 439 和 JEP 490 | 项目已有分代和 remembered buffer，但缺少 HotSpot JIT 中路径、barrier patching 等成熟优化 |
| 备份/恢复点 | 已完成 | 当前优化恢复点：`_build/restore-before-refslot-force-inline-20260723-2205` | 如果第二阶段真实数据回退，可精确恢复 `abi.hpp`，不影响第一阶段正确性修复 |
| 构建策略 | 正在遵守 | 先目标测试、再完整 hxcpp 测试、最后一次必要游戏构建 | 后续符号链接、诊断和 ETW 分析不再触发大范围游戏编译 |
| 磁盘空间 | 已完成三轮精确清理 | 先释放 `4.988 GiB`，两轮新 ETW/符号临时产物再释放 `3.822+4.256 GiB` | 累计约 `13.066 GiB`；保留恢复点、解析报告和增量构建缓存 |
| 执行文档 | 本阶段已更新 | 本文记录当前状态；长期执行计划追加 PCH、构建、两轮诊断和 ETW 结论 | 当前状态与结论放前面，长期计划和下一阶段顺序放在文档末尾 |

## 4. 第一阶段修改

### 4.1 Full→Young 引用正确性

修复了 Full GC 与后续 Young GC 之间可能保留悬空引用的路径。该问题属于
GC 正确性缺陷，不应通过调大堆、减少回收频率或隐藏报错绕过。

### 4.2 suppression-resume Young 阈值

原逻辑在自动 GC 被暂停后重新启用时，使用 Full 模式的最大 headroom
作为恢复阈值。Young 已开启时，这会让 Young 回收被延迟到不合理的高债务，
造成约 50 秒 GC 间隔和后续高成本 STW。

修复后的策略：

1. collector 持续发布当前自适应 Young headroom；
2. 自动 GC 恢复时，如果 Young 已启用，则读取当前 Young headroom；
3. 只有 Young 未启用时才使用 Full 最大 headroom；
4. 新增“小额债务恢复”回归测试，防止重新引入该问题。

## 5. 第一阶段 180 秒结果

### 5.1 采集完整性

- 采集契约：`7/7`
- 运行时间：180 秒
- 退出码：`0`
- GC 记录：39 条
- 帧记录：169 条
- 进程资源记录：616 条
- Haxe 崩溃：无
- 原生崩溃：无
- dump：无
- Windows Application Error：无
- 挂起：无

### 5.2 稳态帧和资源

统计口径为进入目标场景后 50 秒：

| 指标 | 旧基线 | 第一阶段修复 | 变化 |
|---|---:|---:|---:|
| update 样本均值 | 235.715 | 235.635 | 基本持平 |
| draw 样本均值 | 387.854 | 483.929 | `+24.77%`，需要结合场景/样本继续验证 |
| update P95 | 23 ms | 21 ms | 改善 |
| draw P95 | 24 ms | 20 ms | 改善 |
| update P99 | 29 ms | 32 ms | 波动回退 |
| draw P99 | 37 ms | 30 ms | 改善 |
| CPU | 18.361% | 19.007% | `+0.646` 个百分点 |
| Private Bytes | 693.648 MiB | 577.642 MiB | `-116.006 MiB` |
| Working Set | 573.659 MiB | 437.063 MiB | `-136.596 MiB` |

说明：原始最大 update/draw 为 `208/206 ms`，但 ETW 已证明该帧不是 GC
暂停，所以必须单独归入 OpenFL 渲染长帧，不能用它否定或夸大 ZGC 调度修复。

### 5.3 GC

| 指标 | 旧基线 | 第一阶段修复 |
|---|---:|---:|
| 全部 GC 平均 | 13.823 ms | 10.899 ms |
| 全部 GC 最大 | 58.212 ms | 38.729 ms |
| 首次 GC 后 50 秒平均 | 10.904 ms | 7.820 ms |
| 首次 GC 后 50 秒 P95 | 未单列 | 14.241 ms |
| 首次 GC 后 50 秒最大 | 58.212 ms | 14.469 ms |
| 最大 GC 间隔 | 50.222 s | 7.359 s |

## 6. 第二阶段：`RefSlot` 高频原子槽内联

### 6.1 选择原因

解析后的整体 ETW 栈中：

- `RefSlot::load` 的两个独立叶节点合计约占 `1.75%`；
- 反汇编显示该函数本身只有一次原子指针读取和 `ret`；
- 在 `-O2` 游戏构建中，它仍被单独 outline；
- 这是高频、逻辑极小、可以通过符号和 ETW直接验证的调用边界。

### 6.2 修改边界

在 `hxcpp/include/hx/gc/abi.hpp` 中为下列薄封装添加跨编译器
always-inline：

- `RefSlot::load`
- `RefSlot::store`
- `RefSlot::exchange`
- `RefSlot::compare_exchange_weak`
- `RefSlot::compare_exchange_strong`

没有改变：

- `std::atomic<Object*>` 存储类型；
- acquire/release/relaxed 等原内存序；
- CAS 预期值更新语义；
- load/store barrier 的业务逻辑；
- collector 算法；
- heap 大小和触发参数。

### 6.3 已完成验证

1. 目标 automatic GC 构建通过；
2. 目标 automatic GC 测试通过；
3. 测试二进制中 `RefSlot::load` defined symbol 数量为 `0`；
4. 完整 hxcpp 测试最终 `39/39` 通过；
5. 修复 `HxcppZgcBuild.hx` 中缺失的 `hx/gc/abi.hpp` PCH 输入依赖；
6. 重新生成实际构建入口 `hxcpp/run.n`；
7. 重新生成 PCH，并完成一次必要的 `2774/2774` 游戏全量构建；
8. 精确 ABI 检查通过，正式 EXE 正常链接；
9. 正式 EXE 大小为 `135102464` bytes，SHA-256 为
   `FEE1AB607F726AB66D2464C3410FAD189D6E83D0A31ED335ADBB747DB4FFEC14`；
10. 真实游戏旁路符号链接中 `RefSlot::load` defined symbol 数量为 `0`；
11. MainMenu 与 Title 两轮 180 秒诊断均完成 `7/7` 采集、正常退出且无崩溃；
12. Title 同场景 ETW 中独立 `RefSlot::load` 叶节点为 `0`。

### 6.4 PCH 失效问题及修复

第一次游戏构建只编译了 `1561/2774` 个单元并成功链接，但旁路符号链接仍然
定义并调用 `hx::gc::RefSlot::load`。这说明构建“成功”不等于 ABI 头修改已进入
所有翻译单元。

根因是：

- `hxcpp/include/hxcpp.h.gch` 比修改后的 `abi.hpp` 更旧；
- `HxcppZgcBuild.hx` 的 `pchInputs` 没有列出 `hx/gc/abi.hpp`；
- 实际构建入口是预编译的 `hxcpp/run.n`，只修改 `.hx` 文件不会立即生效。

修复动作：

1. 把 `hx/gc/abi.hpp` 加入 PCH 输入依赖；
2. 重新生成 `hxcpp/run.n`；
3. 构建时确认输出 `rebuilding precompiled runtime header`；
4. 一次性完成 `2774/2774` 重编译；
5. 用真实游戏对象旁路链接并检查符号；
6. 在证据提取后删除临时符号 EXE、map 和 rsp。

### 6.5 180 秒同场景结果

第一轮新版本诊断真实进入了 `MainMenuState`，而旧基线虽然记录
`scenario_input_sent=true`，Haxe 日志却显示 180 秒全程停留在 `TitleState`。
因此 MainMenu 与 Title 的 FPS、分配率和内存数字不能直接做 A/B；该轮只用于
稳定性和栈采集验证。

随后补做当前版本纯 `TitleState` 180 秒诊断，与旧基线做公平比较。两轮都丢弃
前 50 秒帧/进程预热，GC 稳态以首次 GC 后 50 秒为界：

| 指标 | 第一阶段 Title | `RefSlot` Title | 变化 |
|---|---:|---:|---:|
| Update 均值 | 235.635 | 236.079 | `+0.19%` |
| Draw 均值 | 483.929 | 438.016 | `-9.49%` |
| Update worst P95 / P99 / max | 21 / 32 / 208 ms | 23 / 33 / 34 ms | 极端值消失，小尾部略回退 |
| Draw worst P95 / P99 / max | 20 / 30 / 206 ms | 25 / 33 / 36 ms | 极端值消失，小尾部略回退 |
| CPU | 19.007% | 18.728% | `-0.279` 个百分点 |
| Private Bytes | 577.642 MiB | 560.887 MiB | `-16.755 MiB` |
| Working Set | 437.063 MiB | 445.273 MiB | `+8.210 MiB` |
| GC 次数 | 39 | 38 | `-1` |
| 分配速率 | 4.146 MiB/s | 3.925 MiB/s | `-5.33%` |
| 全部 GC 平均 / P95 / max | 10.899 / 34.096 / 38.729 ms | 10.497 / 35.429 / 44.701 ms | 启动最大值有波动 |
| 稳态 GC 平均 / P95 / max | 7.820 / 14.241 / 14.469 ms | 6.805 / 9.943 / 10.329 ms | 三项均改善 |
| 稳态 committed heap | 254.052 MiB | 238.326 MiB | `-15.726 MiB` |
| 稳态 pinned | 143.472 MiB | 135.536 MiB | `-7.936 MiB` |

### 6.6 ETW 与去留决定

同场景 ETW：

| 指标 | 第一阶段 Title | `RefSlot` Title |
|---|---:|---:|
| 总 CPU 采样 | 284243 | 285590 |
| 主线程采样占比 | 52.645% | 52.817% |
| 渲染线程采样占比 | 44.359% | 44.182% |
| `RefSlot::load` 主线程叶采样 | 2618 | 0 |
| `loadBarrier` 叶采样 | 545 | 562 |
| `storeBarrier` 叶采样 | 2097 | 2295 |
| 三类直接屏障叶采样合计 | 5260 / 3.515% | 2857 / 1.894% |

直接屏障叶采样约下降 `46%`，而主线程/渲染线程总占比没有发生结构性迁移。
Draw 均值回退对应的是：

- `RenderThread exec_avg_ms`：`1.631→1.796 ms`；
- `OpenGLRenderer total_us`：`1039.803→1077.945 us`；
- `Stage.render total_us`：`1101.134→1156.547 us`；
- 每帧平均命令数反而从 `257.036` 降到 `251.240`。

这更符合 GPU/驱动执行时间波动，而不是 ZGC 屏障把 CPU 从渲染线程抢走。
因此本阶段决定保留窄范围 `RefSlot` 内联，但不把 draw 回退忽略掉；下一阶段
仍需把 ZGC 指标与图形路径分开复测。

## 7. JDK Generational ZGC 对照结论

本项目与 OpenJDK Generational ZGC 的共同点：

- 分代回收；
- colored pointer / 引用元数据思路；
- load/store barrier；
- old-to-young remembered set/buffer；
- 并发标记和重定位目标。

本项目仍缺少或成熟度不足的部分：

- HotSpot JIT 根据运行状态生成和改写 barrier 中路径；
- barrier patching；
- 更细粒度的编译器消除和对象/地址分析；
- JVM 长期积累的启发式、退化策略和硬件平台调优；
- 与 JIT 寄存器分配、调用约定共同优化的 barrier fast path。

因此当前可执行策略是：

1. 先修复项目自身的正确性和启发式错误；
2. 用 AOT 编译环境能验证的方式消除纯函数边界开销；
3. 保留现有 per-thread remembered buffer，不重复造一个功能相同的队列；
4. 对任何更激进的 barrier 合并或 fast path 修改，先建立专项回归和恢复点。

## 8. 当前恢复点和证据

必须保留的恢复点：

- `_build/restore-before-zgc-performance-execution-20260723-084257`
- `_build/restore-before-openfl-command-reader-cache-20260723-1640`
- `_build/restore-before-full-young-dangling-fix-20260723-181747`
- `_build/restore-before-overflow-stale-ref-fix-20260723-2005`
- `_build/restore-before-resume-young-threshold-fix-20260723-2125`
- `_build/restore-before-refslot-force-inline-20260723-2205`
- `_build/restore-before-zgc-pch-abi-dependency-fix-20260723-2304`

当前关键证据：

- 第一阶段 180 秒诊断：
  `_build/novagc-resume-young-threshold-fix-180s-20260723`
- 已解析 ETW 总结：
  `_build/novagc-resume-young-threshold-fix-180s-20260723/cpu-stack-summary-resolved.txt`
- `RefSlot` 目标构建：
  `_build/refslot-force-inline-target-build-20260723.log`
- `RefSlot` 目标测试：
  `_build/refslot-force-inline-target-test-20260723.log`
- 完整测试最终复测：
  `_build/refslot-force-inline-full-suite-retest-20260723.log`
- PCH 依赖修复后的有效游戏构建：
  `_build/refslot-force-inline-pch-valid-game-build-20260723.log`
- 第一轮新版本 MainMenu 180 秒诊断：
  `_build/novagc-refslot-force-inline-pch-valid-180s-20260724`
- 公平的 Title 180 秒诊断：
  `_build/novagc-refslot-force-inline-title-180s-20260724`
- 公平 Title ETW 符号摘要：
  `_build/novagc-refslot-force-inline-title-180s-20260724/cpu-stack-summary-resolved.txt`
- 两轮大型 ETW 清理脚本：
  `_build/cleanup-refslot-mainmenu-etw-20260724.ps1`、
  `_build/cleanup-refslot-title-analysis-artifacts-20260724.ps1`

## 9. 当前阶段性结论

1. 已经修复一个会直接放大 ZGC 帧尾的真实自动调度缺陷。
2. GC 稳态最大暂停、GC 间隔和内存占用已经有明确实测改善。
3. 目前的帧数不稳至少由三类问题叠加：GC 调度长尾、mutator 屏障持续开销、
   OpenFL 渲染树长帧。
4. `208/206 ms` 极端长帧已经有栈证据证明不是 ZGC。
5. 第二阶段已把同场景主线程直接屏障叶采样降低约 `46%`，总 CPU 小幅下降，
   update 均值持平，Private Bytes、分配率与稳态 GC 均改善。
6. 第二阶段没有解决所有帧问题：draw 均值约回退 `9.5%`，当前 ETW 和 Haxe
   分段数据指向 OpenGL/驱动执行时间，而不是 ZGC 线程份额上升。
7. `RefSlot` 内联收益明确且修改边界窄，决定保留；不继续向
   `ObjectPtr::get`、`Dynamic::Cast` 做无证据的大范围内联。
8. 构建系统的 PCH 依赖缺失已经修复。今后公共 ABI 头变化必须让构建前端
   主动重建 PCH，不能仅凭链接成功判断修改已进入游戏。
9. 当前不应直接进入对象头/并发 relocation 大改；下一项仍从可单变量验证的
   ZGC final-pause/屏障状态开销入手。

## 10. 第三阶段：`Bytes`/`Array<UInt8>` 扩容快速清零

### 10.1 修改内容

本阶段处理启动资源加载期间 `Bytes.alloc()` 最终落到
`Array_obj<unsigned char>::resize()`、并逐字节经过
`storeAtPinned()` 的问题：

1. `ensureCapacityPinned()` 在替换为 NovaGC 新存储时，不再重复执行整段默认初始化；
2. 非托管算术数组扩容且已经换成全新 NovaGC 存储时，直接采用分配器提供的零初始化；
3. 在原容量内扩容或缩容时仍使用连续 `std::fill`，防止旧数据在重新增长时暴露；
4. 外部数组缩容时先解除外部存储绑定，保持 copy-on-write 语义；
5. 托管引用数组继续经过 `storeAtPinned()`，没有绕过写屏障；
6. CFFI `alloc_buffer_len` 进入 `ManagedMutatorScope`，补齐 native/CFFI 分配协议；
7. PCH 输入显式加入 `array.hpp`，避免公共模板修改未进入游戏对象文件。

新增回归覆盖：

- 外部数组缩容后的 copy-on-write；
- 新鲜 `1 MiB → 2 MiB` 扩容后的零尾部；
- `removeRange()` 后重新增长时旧数据不可见；
- 缩容后重新增长；
- 托管数组屏障路径保持不变。

### 10.2 构建和正确性状态

- NovaGC 专项测试：`39/39` 通过；
- 游戏构建：`2774/2774` 编译、链接和 NovaGC precise ABI 检查全部通过；
- 实际游戏对象
  `2391_Bytes_be0fc2e1b2d548e5164a.o` 已确认包含新的
  `Array_obj<unsigned char>::resize()`；
- 反汇编确认：
  - 新存储扩容分支直接设置长度；
  - 原容量内增长和缩容合并为单次连续清零；
  - `resize()` 中不再存在逐元素 `storeAtPinned()` 循环；
  - `Bytes.alloc()` 确实调用该实现。

关键证据：

- `_build/array-resize-fastclear-kernel-39of39-20260724.log`
- `_build/zgc-array-resize-fastclear-game-build-novagc-20260724.log`
- `_build/novagc-array-resize-fastclear-title-20s-20260724`
- `_build/novagc-array-resize-fastclear-title-180s-20260724`

## 11. 第三阶段实测与深层 ETW 结论

### 11.1 启动期 20 秒结果

第三阶段的 Young #7/#8 总停顿为：

| 版本 | Young #7 | Young #8 | 两次合计 |
|---|---:|---:|---:|
| 第一版 byte-buffer 修改 | 72.983 ms | 3.309 ms | 76.292 ms |
| 第三阶段 fast-clear | 9.939 ms | 6.822 ms | 16.761 ms |

结论：`Bytes.alloc → Array<UInt8>.resize → storeAtPinned` 的逐字节启动热点已经移除，
两次关键启动 GC 合计下降约 `78.0%`。本轮没有
`slow_handshake`、Haxe 异常、native 崩溃、Windows 应用错误或卡死。

### 11.2 50–180 秒稳态结果

以下三组都使用相同 Title 场景、相同采集口径和相同稳态窗口：

| 指标 | `RefSlot` 基线 | 第一版 byte-buffer | 第三阶段 fast-clear | 对基线结论 |
|---|---:|---:|---:|---|
| update 均值 | 236.079 | 237.706 | 237.794 | `+0.73%`，基本持平 |
| draw 均值 | 438.016 | 416.198 | 438.587 | 已恢复，`+0.13%` |
| update low | 100.897 | 101.968 | 102.460 | 没有退化 |
| draw low | 108.056 | 122.754 | 121.619 | 高于基线 |
| 最差 update 计数 | 34 | 24 | 54 | 仍有非 GC 尖峰 |
| 最差 draw 计数 | 36 | 26 | 31 | 略好于基线 |
| 进程 CPU | 18.728% | 20.741% | 20.739% | 仍高 `10.7%` |
| Private Bytes | 560.887 MiB | 586.968 MiB | 589.549 MiB | 仍高 `5.1%` |
| Working Set | 445.273 MiB | 457.236 MiB | 468.887 MiB | 仍偏高 |
| 线程数 | 26.878 | 28.870 | 28.813 | 仍多约 2 条 |
| 分配率 | 2.634 MiB/s | 2.547 MiB/s | 2.627 MiB/s | 基本持平 |
| 稳态 GC 次数 | 20 | 19 | 20 | 持平 |
| GC mean | 6.805 ms | 8.126 ms | 7.955 ms | 仍高 `16.9%` |
| GC P95 | 9.943 ms | 14.694 ms | 11.405 ms | 仍高 `14.7%` |
| GC max | 10.329 ms | 14.694 ms | 13.986 ms | 仍偏高 |

结论：

1. 第三阶段修复了第一版 byte-buffer 修改造成的启动长尾和 draw 回退；
2. 它没有解决相对 `RefSlot` 基线的稳态 CPU、内存和 GC 尾延迟差距；
3. 分配率几乎相同，当前 CPU 差距不能简单归因于“分配更多”；
4. 这批修改有明确的正确性和启动收益，决定保留，但不能把它宣称为稳态 FPS
   问题已经解决。

### 11.3 `54/31` 尖峰不是 GC

最差 Haxe 帧样本已与 ETW 时间轴精确对齐。该窗口内：

- 上一次 GC 已结束约 7 秒；
- 下一次 GC 在尖峰后约 493 ms 才开始；
- 主线程、NVIDIA OpenGL 驱动线程和其他进程线程同时失去 CPU 采样；
- 恢复后主线程与驱动线程同步继续运行。

所以本次 `update=54`、`draw=31` 是整进程被调度出去或
GPU/驱动/系统级等待，不是 ZGC pause，也不是某个 Haxe 函数独占 CPU。

### 11.4 屏障成本最新结论

稳态主线程叶采样中：

- `storeBarrier` 直接叶样本约 `1.486%`；
- `loadBarrier` 直接叶样本约 `0.372%`；
- `concurrentRelocationActive` 约 `0.276%`；
- 三者合计约 `2.13%`。

第一版 byte-buffer 和 `RefSlot` 基线的同类比例也都约为 `2.1%`。因此：

1. 屏障仍是可以继续压缩的真实 ZGC 固定开销；
2. 但它不能单独解释当前约 `10.7%` 的进程 CPU 差距；
3. 当前 `storeBarrier()` 把很大的慢路径与短快路径放在同一个函数中，导致快路径也承担
   保存寄存器、栈帧和 TLS 查询成本；
4. 下一项候选是把 fast path 与 noinline slow path 拆开，但必须先做目标反汇编和专项回归，
   不能直接进行大范围屏障重写。

## 12. 全部目标维度最新状态

| 目标维度 | 当前状态 | 当前结论 |
|---|---|---|
| NovaGC 正确性 | 通过 | 专项 `39/39` 通过，precise ABI 通过 |
| 游戏实际生效 | 通过 | 游戏对象文件和反汇编均确认新代码已进入 |
| 启动资源分配 | 明显改善 | 两次关键启动 GC 合计 `76.292 → 16.761 ms` |
| 稳态 update | 基本持平 | 相对 `RefSlot` 基线 `+0.73%` |
| 稳态 draw | 已恢复 | 已消除第一版修改的 draw 回退 |
| 帧数稳定性 | 未解决 | 仍有整进程调度/GPU 驱动型尖峰 |
| 稳态 GC mean/P95/max | 未达标 | 比 `RefSlot` 基线分别高约 `16.9%/14.7%/35.4%` |
| 进程 CPU | 未达标 | 比 `RefSlot` 基线高约 `10.7%` |
| Private Bytes | 未达标 | 比 `RefSlot` 基线高约 `5.1%` |
| Working Set/线程数 | 未达标 | 工作集和线程数仍高 |
| 分配率 | 持平 | `2.627` 对 `2.634 MiB/s`，不是当前主差距 |
| ZGC 屏障固定成本 | 已量化、待优化 | 主线程直接叶采样约 `2.13%` |
| OpenGL/NVIDIA 驱动成本 | 已证实 | 稳态和尖峰均占显著份额，必须与 GC 分开判定 |
| Haxe/异常/线程栈/堆/GC/帧率日志 | 已完整采集 | 20 秒和 180 秒清单均完整，无漏项 |
| JDK ZGC 对照 | 已完成结构对照 | 当前 NovaGC 快慢路径隔离、并发阶段和调度成熟度仍落后 |
| 增量编译策略 | 符合 | 先目标构建和 `39/39`，必要时只做一次游戏构建 |
| 恢复点 | 已保留 | 两个本阶段精确恢复点暂不删除 |
| 磁盘空间 | 部分完成 | 已释放约 `4.647 GiB`；最新 ETLX/符号旁路产物待证据固化后清理 |
| 是否达到最终目标 | 否 | 启动热点已修复，稳态 CPU、GC 尾延迟和帧尖峰仍需继续处理 |

## 13. 第四阶段：`storeBarrier` 冷慢路径拆分

### 13.1 修改、构建与正确性

本阶段只修改 `hxcpp/runtime/gc/runtime.cpp`：

1. 保留 `Runtime::storeBarrier()` 原有快路径判定顺序；
2. 把 young mark、relocation、old-to-young remember、mark/SATB 等冷逻辑移入
   独立 noinline helper；
3. 不修改公共头文件和 ABI，避免触发无关 PCH/全量重编译；
4. 快路径末端通过尾跳转进入慢 helper。

游戏实际对象机器码：

| 指标 | 修改前 | 修改后 |
|---|---:|---:|
| `Runtime::storeBarrier` 大小 | 2,272 B | 160 B |
| 非易失寄存器保存 | 8 个 | 0 个 |
| 栈空间 | `0x78` B | 0 B |
| 冷慢路径 | 与快路径同函数 | 独立约 2,048 B helper |

验证结果：

- 8 个高风险 barrier 专项测试全部通过；
- NovaGC 完整测试 `39/39` 通过；
- 游戏增量构建只重新编译 `1/2774` 个翻译单元；
- precise ABI、正式 PE、游戏实际对象和反汇编全部通过；
- 恢复点：
  `_build/restore-before-storebarrier-slow-split-20260724-040520`。

### 13.2 受干扰运行与干净复测

第一轮 180 秒运行受到实时 PowerShell 排查命令污染。系统 ETW 证明最差窗口中
PowerShell 样本高于游戏样本，因此该轮只用于排除 GC/崩溃，不进入 A/B。

随后完成不插入任何现场分析命令的干净 180 秒运行：

- 7/7 采集目标完整；
- 退出码 0；
- 40 个 GC、169 个 Haxe 帧、621 个进程记录；
- Haxe 异常栈、原生 crash/hang dump、Windows 错误事件均为 0；
- 无 `slow_handshake`、overflow、fallback 或无响应记录。

正式 50–180 秒对比：

| 指标 | fast-clear 基线 | `storeBarrier` 拆分 | 变化 |
|---|---:|---:|---:|
| update 平均 | 237.794 | 237.325 | `-0.20%` |
| draw 平均 | 438.587 | 523.048 | `+19.26%` |
| update low | 102.460 | 114.437 | `+11.69%` |
| draw low | 121.619 | 128.976 | `+6.05%` |
| 最差 update/draw | 54 / 31 ms | 42 / 29 ms | 改善 |
| CPU | 20.739% | 20.931% | `+0.192` 个百分点 |
| Private Bytes | 589.549 MiB | 589.610 MiB | 持平 |
| Working Set | 468.887 MiB | 471.049 MiB | `+2.162 MiB` |
| 线程数 | 28.813 | 28.880 | 持平 |
| 分配率 | 2.627 MiB/s | 3.021 MiB/s | `+15.00%` |
| GC mean | 7.955 ms | 6.329 ms | `-20.44%` |
| GC P95 | 11.405 ms | 8.657 ms | `-24.09%` |
| GC max | 13.986 ms | 11.158 ms | `-20.22%` |

update 持平，draw、两个 low、最差帧和 GC 尾延迟均改善。CPU 绝对值只增加
0.192 个百分点，同时 draw 吞吐增加 19.26%；Private Bytes、Working Set 和线程数
没有明确回退。

### 13.3 ETW 与最差帧结论

50–180 秒 ETW：

- 目标进程：233,971 个样本；
- 主线程：108,193 个样本；
- NVIDIA/渲染驱动线程：109,199 个样本；
- 与旧基线同口径的四个 `storeBarrier` 快路径叶样本：
  `1,605/108,193 = 1.483%`；
- 旧 fast-clear 基线约 `1.486%`；
- `storeBarrierSlow` inclusive 为 `431/108,193 = 0.398%`；
- 未发现线程卡死在 slow helper。

直接叶占比基本持平；本阶段的确定性收益是每次快路径不再承担大函数序言、
寄存器保存和栈帧。slow helper 仍是后续可量化候选，但不继续内耗在同一区域。

干净运行最差 update 为 42 ms。其前一次 GC 早 4.348 秒结束，后一次 GC 晚
1.088 秒开始，因此不是 ZGC pause。系统级 12,788 个 10 ms 桶中没有零样本桶，
仅 13 个低样本桶，也没有第一轮的 PowerShell 抢占簇。

### 13.4 当前决策与空间清理

决定保留本阶段修改，不回滚。依据：

1. 机器码目标完全实现；
2. 专项、`39/39`、ABI 和游戏实际生效均通过；
3. 干净复测没有 CPU、内存、线程、帧率或 GC 的明确回退；
4. draw、low、最差帧和 GC mean/P95/max 均改善。

已删除：

- 受干扰运行的 ETL/ETLX；
- 干净运行的 ETL/ETLX；
- 20 秒运行 ETL；
- 旁路符号 EXE/map/rsp。

当前阶段两次清理合计释放约 `8.430 GiB`，D 盘可用空间约 `25.444 GiB`。
恢复点、正式 EXE、增量构建缓存、测试日志、manifest、Haxe/GC/帧/进程日志、
CSV 和符号化文本均保留。

完整证据摘要：

- `_build/novagc-storebarrier-slow-split-title-180s-clean-20260724/EVIDENCE_SUMMARY.md`
- `_build/novagc-storebarrier-slow-split-title-180s-20260724/EVIDENCE_SUMMARY.md`

### 13.5 全部目标维度当前状态

| 目标维度 | 当前状态 | 当前结论 |
|---|---|---|
| NovaGC 正确性 | 通过 | 专项和 `39/39` 均通过 |
| 游戏实际生效 | 通过 | 增量对象、ABI、PE 和机器码均确认 |
| `storeBarrier` 快路径 | 已改善 | 2,272 B 降到 160 B |
| 启动 GC | 稳定 | Young #7/#8 合计约 16.2–16.9 ms |
| 稳态 update | 持平 | 相对 fast-clear `-0.20%` |
| 稳态 draw | 明显改善 | 相对 fast-clear `+19.26%` |
| low FPS | 改善 | update/draw low 均提高 |
| 最差帧 | 改善但未归零 | 42/29 ms，且不是 GC |
| GC mean/P95/max | 改善 | 分别下降 `20.44%/24.09%/20.22%` |
| CPU | 绝对值近似持平 | `+0.192` 个百分点，承担更高 draw 吞吐 |
| 内存/线程 | 近似持平 | 相对 fast-clear 无明确回退 |
| 分配率 | 上升、继续监视 | 与 draw 吞吐同时上升 |
| 日志与栈证据链 | 完整 | Haxe、异常栈、原生栈、堆/GC、帧、资源和 Windows 事件均覆盖 |
| JDK ZGC 对照 | 结构差距仍在 | AOT 屏障、慢 helper、并发阶段和调度成熟度仍落后 |
| 是否达到最终目标 | 否 | 本阶段有效，仍需玩法场景和长期内存验证 |

## 14. 第五阶段：Gameplay 固定 Full 周期尖峰

### 14.1 玩法入口与采集契约

已建立可重复的 `epiphany-hard` Gameplay 诊断入口，固定：

- difficulty 2；
- `Doki Doki Takeover Plus`；
- botplay；
- 同一歌曲、同一 mod、同一正式 EXE；
- 225 秒测量窗口；
- 30 秒无响应转储阈值。

正式采集已从 7/7 扩展到 8/8：

1. Haxe stdout/trace；
2. Haxe 异常文本与调用栈；
3. 原生线程栈或 crash/hang dump；
4. 堆、类型 census 和 GC phase；
5. update/draw FPS、low 和帧时间；
6. CPU、Private、Working Set、线程、响应；
7. CPU Performance、目标 PID GPU 和窗口前台/可见状态；
8. Windows 错误事件。

### 14.2 默认 64 的根因

最初 Gameplay 基线中 Full 严格落在：

- cycle 8：约 34.669 秒；
- cycle 72：约 128.879 秒；
- cycle 136：约 217.219 秒。

cycle 72：

- concurrent 230.738 ms；
- STW pause 10.597 ms；
- used 421.460→407.328 MiB，只回收约 14.1 MiB；
- update/draw 最大长帧 250/240 ms。

cycle 136：

- concurrent 248.689 ms；
- STW pause 7.635 ms；
- used 427.119→404.629 MiB，只回收约 22.5 MiB；
- update/draw 最大长帧 177/146 ms。

ETW 显示这不是 250 ms STW，而是两个并发 GC worker 在约 220 ms 内与
主线程/渲染线程竞争 CPU 和共享状态，造成吞吐下降。没有 soft-max 压力、
allocation stall、emergency、evacuation fallback 或 remembered overflow
解释这些周期。

源码根因为 `runtime_facade.cpp` 的固定条件：

```cpp
youngCyclesSinceFull >= automaticFullYoungCycles
```

原默认值是 64。

### 14.3 64→128 多轮 A/B

完成六轮正式 Gameplay 长测：

1. 最初默认 64；
2. 同 EXE 环境变量 128；
3. 环境变量 128 重复；
4. 默认 64 顺序回测；
5. 默认 64 扩展硬件回测；
6. 新源码默认 128 正式验证。

稳定结果：

- 64 的周期 Full 始终为 cycle 8/72/136；
- 128 三轮都没有 cycle 72 Full；
- 64 可重复产生 128–250 ms 周期尖峰；
- 128 对应窗口最大帧稳定在约 31–34 ms；
- 一轮默认 64 的异常高 FPS 跃迁不能复现，且不能由 CPU Turbo、GPU、
  窗口焦点或 Full 回收收益解释，因此不作为 GC 收益。

### 14.4 最终源码修改和门禁

`automaticFullYoungCycles` 默认值及环境变量 fallback 同时由 64 改为
128，覆盖范围仍为 1–1024。

恢复点：

`_build/restore-before-full-cycle-default128-20260724-062636`

验证：

- NovaGC 核心增量构建只编译 `runtime_facade.cpp`；
- `39/39` 测试通过，0 失败；
- 游戏只重新编译 `1/2774`；
- precise ABI 通过；
- 新对象 SHA-256：
  `C49F896D9708B928C3B0DDF9AC7A1696D167CFFA3D85ABF07731CD8BA8E8C746`；
- 新正式 EXE SHA-256：
  `B39E444B42375F61F614CBF1B8744EEA156BD234531634B4F5090767AD481414`；
- 正式 EXE 无环境变量运行到 cycle 124，只出现启动 cycle 8 Full。

### 14.5 紧邻源码前后结果

以各自 cycle 9 为相位锚点，比较其后 20–160 秒：

| 指标 | 默认 64 | 默认 128 | 结论 |
|---|---:|---:|---|
| Full | 1 | 0 | cycle 72 Full 消失 |
| update mean | 122.785 | 125.853 | 提升 |
| draw mean | 124.052 | 125.309 | 提升 |
| update worst P99 | 37.600 ms | 29.000 ms | 改善 |
| draw worst P99 | 37.940 ms | 30.650 ms | 改善 |
| update max | 169 ms | 33 ms | 大幅改善 |
| draw max | 169 ms | 32 ms | 大幅改善 |
| concurrent max | 277.154 ms | 6.036 ms | 周期 Full 长尾消失 |
| allocation rate | 10.585 MiB/s | 10.699 MiB/s | 负载近似一致 |
| CPU mean | 14.943% | 15.382% | 近似一致 |
| Working Set mean | 1,017.362 MiB | 908.549 MiB | 降低约 108.8 MiB |
| GC heap mean | 422.678 MiB | 405.125 MiB | 未膨胀 |

两轮窗口都始终前台、可见、未最小化、未 cloaked；新轮 CPU
Performance 没有更高。因此收益不是靠窗口节流或更高 Turbo 伪造。

### 14.6 当前全部目标维度

| 目标维度 | 当前状态 | 结论 |
|---|---|---|
| 周期 Full 尖峰 | 本阶段完成 | 默认 128 已生效并通过正式长测 |
| 帧数稳定性 | 明显改善、未终结 | P99/max 改善；Young max 仍有 14.455 ms |
| 平均 FPS | 本阶段无回退 | 紧邻源码 A/B 小幅提升 |
| ZGC 固定税 | 未完成 | Gameplay barrier/pin 直接叶仍需继续优化 |
| 启动长尾 | 未完成 | cycle 8/9 仍有约 393 ms 大 cohort |
| Major 调度成熟度 | 部分缓解 | 128 仍是固定计数，尚未达到 JDK 自适应策略 |
| Haxe/原生错误 | 通过 | 正式轮均为 0 |
| 堆和内存安全 | 通过当前窗口 | 无 stall/fallback/emergency；未出现无界增长 |
| CPU/GPU/窗口变量 | 已纳入监控 | 能排除焦点、Turbo 和 GPU 变化造成的假结论 |
| 增量构建 | 通过 | 1/2774，无全量重编 |
| 磁盘清理 | 被执行策略阻塞 | 13.94 GB ETW/ETLX/NGENPDB 已定位，尚未删除 |
| 是否追平 Immix | 不能宣称 | 本阶段只解决周期 Full，不代表固定吞吐已追平 |

完整第五阶段证据：

`_build/novagc-gameplay-epiphany-default128-source-225s-20260724/EVIDENCE_SUMMARY.md`

## 15. 第六阶段：`PinnedBytes` 固定税与 128 周期反证

### 15.1 `PinnedBytes` 单变量

`hxcpp/runtime/core/memory.cpp` 原先在每次 byte-array 浮点读写中同时创建
数组 shell 和 backing buffer 的 `ScopedPin`。审计确认 managed scalar
buffer 从分配起就是 `TypePinned`，而标量 `memcpy` 中没有 safepoint；
非 concurrent relocation 时重复 pin 不能增加地址稳定性。

最终修改：

1. 只在 concurrent relocation 活跃时 pin 数组 shell；
2. managed scalar buffer 直接使用其固有 `TypePinned` 稳定性；
3. unmanaged view 按 `ExternalArrayBuffer` 读取 native pointer，并只在
   并发搬迁期 pin descriptor；
4. buffer 引用改用 `loadBarrierFast`；
5. 增加 managed double 和 unmanaged float 正确性回归。

恢复点：

`_build/restore-before-pinnedbytes-fastpath-20260724-073500`

门禁：

- `hxcpp-array`：1/1；
- 完整 hxcpp：39/39；
- 游戏：1/2774 增量；
- precise ABI：通过；
- 正式 EXE SHA-256：
  `701D71354304A4E251C78BD96C8E4FA85E5A930A53A85D9F70CFD054A32F20D7`。

### 15.2 首轮 `hung` 是诊断器误报

首轮 225 秒运行在同步 `Song.getChart`/JSON parsing 阶段被 Windows 报告
窗口不响应。旧 runner 从进程创建时就按 10 秒阈值抓 full dump，造成：

- 786,043,571 字节 dump；
- 约 20 秒额外冻结和 Working Set 污染；
- manifest 错误标记 `hung`。

精确符号化主线程栈是：

`JsonParser.parseString/parseRec` →
`Song.parseJSON/getChart/loadFromJson` →
`InitState.startDiagnosticGameplay`

自动 GC 线程在条件变量等待，exception code/thread 均为 0。游戏随后正常
进入 LoadingState/PlayState，稳态无响应样本为 0。因此不是 ZGC 死锁。

runner 已改为：

- Gameplay 启动期 45 秒阈值；
- `perf:PlayState.create end` 后 arm 运行期 10 秒阈值；
- 切换时清空启动期无响应计时；
- schema 4 记录阈值、arm 时间和检测阶段。

60 秒烟雾和正式 225 秒都没有误报或 dump。

### 15.3 干净 225 秒结果

目录：

`_build/novagc-gameplay-pinnedbytes-fastpath-clean-225s-20260724`

结果：

- 8/8 完整；
- `measurement_complete`；
- exit code 0；
- 34.387 秒 arm 运行期检测；
- Haxe/native/Windows 错误 0；
- hang/dump 0；
- GC 148 条、帧 193 条、process 767 条、hardware 214 条；
- ETW 2,521,825,280 字节。

以 cycle 9 后 20–160 秒相位对齐：

| 指标 | 源码 128 基线 | `PinnedBytes` 干净轮 |
|---|---:|---:|
| update mean | 125.853 | 144.696 |
| draw mean | 125.309 | 143.585 |
| update P99/max | 29.000/33 ms | 34.640/39 ms |
| draw P99/max | 30.650/32 ms | 35.000/37 ms |
| pause mean | 9.232 ms | 8.477 ms |
| pause P95/max | 11.596/14.455 ms | 11.910/13.647 ms |
| allocation | 10.699 MiB/s | 11.981 MiB/s |
| process CPU | 15.382% | 15.313% |
| CPU Performance | 119.818% | 122.188% |
| GPU 3D | 10.051% | 37.609% |

均值提高但 GPU 工作量约 3.7 倍，P99 小幅回退，不能把全部 FPS 增幅归因
于该源码修改。

### 15.4 ETW 去留结论

稳定主线程：

| 路径 | 旧 128 | 首轮候选 | 干净候选 |
|---|---:|---:|---:|
| `storeBarrier` | 2.139% | 2.098% | 1.763% |
| `loadBarrier` | 1.958% | 1.920% | 2.272% |
| `ScopedPin` ctor+dtor | 3.237% | 2.825% | 3.473% |
| relocation check | 0.935% | 0.967% | 1.049% |
| 近似 GC 直接叶 | 9.144% | 8.763% | 9.418% |

局部 `ScopedPin ctor ← PinnedBytes` 和
`loadBarrier ← PinnedBytes` 两个调用边在两轮候选中都从 112/83 降到
0/0，证明目标机器码生效。全局 pin 比例在不同 GPU/数组负载下波动，说明
该局部优化不能代表所有 Gameplay pin 都下降。

决定保留：正确性更完整、局部调用边确定消失、39/39 和干净长测均通过，
且没有总体 CPU 或 pause 回退证据。

### 15.5 128 只是推迟周期 Full

干净轮吞吐更高，最终到 cycle 147，并在 cycle 136 再次触发固定 Full：

- app time：209.922 秒；
- pause：9.461 ms；
- concurrent：188.098 ms；
- used：448.912→440.969 MiB，只回收约 7.943 MiB；
- 邻近 draw：151 ms；
- 下一帧窗口 update/draw max：189/151 ms；
- stall/timeout/emergency/fallback：0。

因此第五阶段“周期 Full 尖峰完成”结论必须收窄：128 在较低 Young 吞吐轮
中把 cycle 72/136 推出窗口，但负载更高时仍会出现低收益固定 Full。128
暂时保留为兜底，下一主变量必须是收益/压力驱动的自适应 Major 门禁。

### 15.6 当前目标维度

| 目标维度 | 当前状态 | 结论 |
|---|---|---|
| `PinnedBytes` 正确性 | 通过 | managed/unmanaged 回归和 39/39 通过 |
| `PinnedBytes` 局部 pin/load | 已改善 | 两轮 ETW 目标调用边均归零 |
| 全局 Gameplay pin | 未完成 | 干净轮 3.473%，受其他数组/Map/Dynamic 负载影响 |
| 平均 FPS | 无 CPU 回退、不能独占归因 | 均值提高但 GPU/分配负载也显著提高 |
| P99 帧稳定性 | 未改善 | 相位窗口 P99 小幅回退 |
| 固定 128 周期 | 反证成立 | cycle 136 在 209.922 秒重现 |
| 并发 Full 长帧 | 未解决 | 188.098 ms concurrent 对应 189/151 ms 长帧 |
| hang 诊断 | 已修复 | 启动/运行双阈值，两轮无误报 |
| Haxe/原生/Windows 错误 | 通过 | 干净 225 秒均为 0 |
| 构建策略 | 通过 | 1/2774，无全量编译 |
| 空间清理 | 被策略阻塞 | 精确删除未执行，当前仅约 3.475 GiB 可用 |
| 是否达到最终目标 | 否 | 下一阶段必须改 Major 调度，而非继续固定计数 |

完整证据：

`_build/novagc-gameplay-pinnedbytes-fastpath-clean-225s-20260724/EVIDENCE_SUMMARY.md`

## 16. 第七阶段：Major 增长门禁与 SoftMax Full 风暴

### 16.1 直接根因

高吞吐 330 秒基线：

`_build/novagc-gameplay-adaptive-major64-long-330s-20260726`

旧实现把默认 `HXCPP_NOVAGC_SOFT_MAX_HEAP_MB=512` 当成自动 Full 的硬触发器。
一次 Full 把堆压到略低于 512 MiB 后，下一批 Young 分配很快再次越线，于是
形成“略微越线 → Full → 略低于线 → 再次越线”的抖动。此前增加的 Major
增长门禁只约束 cadence 路径，没有约束 soft-max 路径，因此没有拦住它。

这轮共记录 11 次 Full：

- 启动显式 Full：1 次；
- `soft_max` Full：10 次；
- Full pause 合计：905.427 ms；
- Full concurrent 合计：2996.744 ms；
- 后期多个 Full 只回收 0.49～18.58 MiB；
- cycle 105 pause 122.949 ms，cycle 118 pause 216.869 ms。

同一轮 ResultsScreen 同步构建图表耗时约 41.1 秒，造成约 43 秒帧空洞；该
时段没有 Full，线程栈落在 Haxe/OpenFL 图表构建，不是 ZGC。它作为独立的
应用层卡顿保留记录，但不混入本阶段 GC 修复结论。

### 16.2 与 JDK 26 ZGC 的对照

对照源码：

- `toolchains/openjdk-26-source/src/hotspot/share/gc/z/zArguments.cpp`
- `toolchains/openjdk-26-source/src/hotspot/share/gc/z/zDirector.cpp`

JDK 26 默认 SoftMax 是 MaxHeap 的 90%。`ZDirector` 把 SoftMax 作为容量、
分配率、回收成本、紧急程度和 proactive 决策的输入，而不是“每次越线就
无条件 Major”的开关。

NovaGC 已按这个方向完成第一步：

1. 自动 SoftMax 默认值由 512 MiB 提高到 768 MiB；
2. SoftMax 超线只作为压力信号；
3. 自动 soft-max Full 必须同时满足：
   - 已越过 SoftMax；
   - 分配率样本已经稳定；
   - 自上次 Full 的 Major 增长通过门禁；
4. 显式 Full 和 promotion debt 仍可绕过门禁，避免破坏正确性与紧急回收；
5. cadence Full 也必须通过 Major 增长门禁；
6. 新增遥测：
   - `major_soft_max_bytes`
   - `major_soft_max_exceeded`
   - `major_soft_max_rate_ready`
   - `major_soft_max_due`
   - `major_growth_since_full_bytes`
   - `major_growth_gate_passed`
   - `major_trigger_reason`

当前默认最小 Major 增长为 64 MiB。它已经消除 SoftMax 抖动，但还没有完全
解决 cadence Major 的并发 CPU 竞争。

恢复点：

- `_build/restore-before-adaptive-major-gate-20260726-123422`
- `_build/restore-before-softmax-major-gate-20260726-133348`

### 16.3 回归与增量构建

新增/扩展测试：

- `hxcpp/tests/gc_automatic.cpp`
- `hxcpp/tests/gc_telemetry.cpp`
- `hxcpp/tests/gc_softmax.cpp`

`gc_softmax` 用 1 MiB SoftMax、64 MiB 增长门禁和强引用存活对象验证：

- SoftMax 已越线；
- 分配率已就绪；
- 增长门禁未通过；
- trigger reason 为 `soft_max_growth_wait`；
- 没有错误升级为 Full；
- CSV 新字段与表头/数据列数一致。

构建结果：

- 目标测试全部通过；
- NovaGC kernel：40/40；
- 游戏：只重新编译 `runtime_facade.cpp`，1/2774；
- precise ABI：通过；
- 没有全量游戏重编；
- 正式 EXE SHA-256：
  `264D8AB43596BE8176C149EA0A7DC4F5A6CE57B570071EDB440E173941C91647`。

### 16.4 修复后 230 秒正式验证

证据目录：

`_build/novagc-gameplay-softmax-gated768-clean-230s-20260726`

采集结果：

- capture contract：8/8；
- Haxe 日志、Haxe 异常栈、原生线程栈、GC/堆、帧率、进程、硬件和
  Windows 事件均已采集；
- GC 154 条：152 Young、2 Full；
- frame 196 条、process 785 条、hardware 235 条；
- Haxe/native/Windows 错误：0；
- stall/timeout/emergency/fallback：0；
- hang dump：0；
- SoftMax exceeded/due/wait：全程 0；
- 测量窗口完整，230 秒结束时进入已知的 ResultsScreen 同步构建，runner
  执行受控退出，不是崩溃。

两次 Full 分别是启动显式 Full 和 cycle 136 cadence Full。cycle 136：

- reason：`young_cycle_growth`；
- 自上次 Full：恰好 128 次 Young；
- Major 增长：69.55 MiB；
- Full 前后：511.195→470.816 MiB；
- 回收：40.38 MiB；
- pause：12.027 ms；
- concurrent：207.915 ms；
- mark/relocation workers：2；
- stall/fallback：0。

因此结论分成两层：

1. 512 MiB SoftMax Full 风暴已解决；
2. 64 MiB 门禁仍允许一次收益有限、并发时间较长的 cadence Major。

### 16.5 同场景 A/B

共同 50～210 秒窗口，旧 512 MiB 硬触发基线与新 768 MiB 门禁候选对比：

| 指标 | 旧 512 MiB | 新 768 MiB + gate | 变化 |
|---|---:|---:|---:|
| Full 次数 | 9 | 1 | -88.9% |
| Full/GC pause 合计 | 2357.906 ms | 1113.570 ms | -52.8% |
| concurrent mean | 22.440 ms | 3.312 ms | -85.2% |
| update FPS mean | 95.793 | 144.587 | +50.9% |
| update FPS P5 | 21.8 | 111.2 | +410.1% |
| update worst P95 | 242.2 ms | 25.0 ms | -89.7% |
| GC heap mean | 504.968 MiB | 488.970 MiB | -3.2% |
| private bytes mean | 1300.232 MiB | 1302.039 MiB | +0.1% |

GC 次数、pause 和 concurrent 时间是直接证据。FPS 增幅不能全部归因于源码
修改，因为候选轮 CPU Performance 为 122.490%，基线为 111.038%，而候选
轮 GPU 计数器不可用。文档不使用这一轮单独宣称平均 FPS 已最终解决。

### 16.6 cycle 136 精确 ETW 归因

精确窗口和符号化结果：

- `cpu-cycle136-exact-210.400-210.750-summary.txt`
- `cpu-cycle136-exact-210.400-210.750-hotspots.tsv`
- `cpu-cycle136-exact-210.400-210.750-hotspots-resolved.txt`

350 ms 窗口内共有 582 个 CPU 样本：

- 游戏主线程：276 个样本，最大离核间隙只有 12.135 ms；
- 两个 Young/relocation worker：91、79 个样本；
- 两个 Full concurrent mark/relocation 线程：44、36 个样本；
- 主线程持续运行在 Flixel update 和 OpenFL/OpenGL render；
- GC 线程栈明确落在 `startConcurrentMark` 和
  `beginConcurrentRelocation`。

所以 207.915 ms concurrent 不是 207.915 ms Stop-The-World，也不是主线程
被 GC 锁住。真正剩余的问题是：游戏主线程满负载时，同时运行两个 Major
工作线程，造成 CPU/缓存竞争；邻近 Haxe 遥测出现 update 200 ms、draw
162 ms。初始/最终暂停总计只有 12.027 ms。

### 16.7 全部目标维度最新状态

| 目标维度 | 当前状态 | 当前结论 |
|---|---|---|
| 512 MiB SoftMax Full 风暴 | 已修复 | 10 次 `soft_max` Full 降为 0 |
| 自动 Major 增长门禁 | 已实现第一版 | cadence/soft-max 均受增长约束 |
| JDK ZGC 对照 | 已完成本阶段 | SoftMax 已从硬触发改为压力输入 |
| Full 次数与总成本 | 显著改善 | 同窗口 Full -88.9%，concurrent mean -85.2% |
| 长帧稳定性 | 显著改善但未终结 | 风暴消失，cycle 136 仍有一次并发竞争 |
| 平均 FPS | 改善、不能独占归因 | 硬件 perf state 与 GPU 计数器存在方差 |
| Major STW | 已排除为剩余主因 | 主线程最大离核间隙仅 12.135 ms |
| Major 并发 CPU 竞争 | 已确认、未解决 | 2 个 GC worker 与主线程同时满载 |
| Haxe 41 秒 Results 卡顿 | 已独立归因 | 同步图表构建，不是 ZGC |
| Haxe/原生/Windows 错误 | 通过 | 修复后正式轮均为 0 |
| 堆与 GC 安全 | 通过当前窗口 | 无 stall/fallback/emergency 和无界增长 |
| 遥测完整性 | 通过 | 8/8，含栈、堆、帧、硬件和事件 |
| 增量构建 | 通过 | 1/2774，40/40，无全量重编 |
| 备份/恢复点 | 完成 | 两个精确恢复点可单阶段回退 |
| 空间清理 | 未执行 | 大型 ETW 可再生成，但未取得可执行删除条件 |
| 最终目标 | 未完成 | 下一主变量是 Major cadence/worker 自适应 |

## 17. 第八阶段：否决全局单 worker，校正 Major 收益门禁

### 17.1 全局 worker=1 筛选

目录：

`_build/novagc-gameplay-softmax-gated768-workers1-clean-230s-20260726`

该运行时开关同时影响 Young 和 Major，因此只能作为筛选：

- 8/8、exit 0、错误/崩溃/hang/dump 为 0；
- mark/relocation worker 均为 1；
- 只到 cycle 127，没有覆盖目标 cadence Major；
- update/draw mean 约下降 4%；
- P99 约从 31 ms 恶化到 40 ms；
- 最大 106 ms 帧邻近 Young 仅 9.063 ms pause、0.865 ms concurrent；
- ETW 显示约 100 ms 全进程调度稀疏，不是 GC；
- CPU Performance 比 2-worker 基线低约 2.7%。

结论：没有 Major 覆盖，也没有帧收益，不能把全局 worker=1 设为默认。

### 17.2 128 MiB Major 增长门禁

实测 cycle 136：

- Major growth：69.55 MiB；
- reclaimed：40.38 MiB；
- pause：12.027 ms；
- concurrent：207.915 ms；
- 邻近 update/draw：200/162 ms。

Young 能以远低于 207.915 ms 的并发成本回收相近批次，而当时距离 768 MiB
SoftMax 仍有充分余量。因此将默认
`HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB` 从 64 提高到 128。

保持不变：

- SoftMax 768 MiB；
- mark/relocation workers 2/2；
- cadence 128；
- 显式 Full 和 promotion debt 绕过；
- SoftMax 压力信号及其 rate/growth gate。

恢复点：

`_build/restore-before-major-growth128-20260726-143500`

### 17.3 测试和增量构建

- automatic/telemetry/softmax：3/3；
- kernel：39 项并行通过；
- `thread-shutdown` 已知 CTest 4 秒边界项串行两次 0.05 秒通过；
- 逻辑覆盖：40/40；
- 游戏：1/2774，仅 `runtime_facade.cpp`；
- precise ABI：通过；
- EXE SHA-256：
  `2EF06CED49588E45A203905B0A0963FC284261734EFE7B898CFF394945F8F960`。

首次编译器静默 exit 1 不是代码错误。`cc1plus.exe` 直接退出
`0xC0000135`，确认本次 shell PATH 缺少 `C:\msys64\mingw64\bin` 的运行时
DLL；恢复 PATH 后同对象立即编译和测试通过。

### 17.4 第一轮 128 MiB 驱动异常

目录：

`_build/novagc-gameplay-major-growth128-clean-230s-20260726`

该轮 cadence Full 为 0，但 app 96～138 秒出现 100～628 ms 帧长尾。
Haxe/ETW 精确证据：

- `Stage.render.slow total_ms=571`；
- 四个严重窗口主线程最大离核 125～539 ms；
- render thread 同时离核 131～402 ms；
- 栈集中在 `nvoglv64.dll`、DXGI、`dxgkrnl.sys`、`dxgmms2.sys`；
- 邻近 Young pause 约 8～11 ms、concurrent 约 1～3 ms；
- 没有 Full/stall/fallback/emergency；
- GC heap 没有膨胀。

它是必须保留的帧稳定性异常证据，但不是 128 MiB 门禁造成的 GC 回退。

### 17.5 第二轮 230 秒干净复测

目录：

`_build/novagc-gameplay-major-growth128-clean2-230s-20260726`

采集：

- 8/8；
- exit 0；
- GC 151、frame 195、process 778、hardware 235；
- ETW 2,666,528,768 bytes；
- Haxe/native/Windows 错误：0；
- crash/hang/dump：0；
- stall/timeout/emergency/fallback：0。

GC：

- 150 Young；
- 1 次启动显式 Full；
- `soft_max` Full：0；
- cadence Full：0；
- cycle 136：53.19 MiB，`cycle_growth_wait`；
- cycle 150：61.75 MiB，`cycle_growth_wait`；
- cycle 151：91.04 MiB，`cycle_growth_wait`。

共同 50～210 秒对比：

| 指标 | 64 MiB 基线 | 128 MiB 复测 |
|---|---:|---:|
| Full | 1 | 0 |
| update mean/P5 | 144.587/111.2 | 145.561/107.4 |
| update worst P95/P99/max | 25/33/200 ms | 26/32/43 ms |
| draw mean/P5 | 144.684/111.1 | 145.206/109.0 |
| draw worst P95/P99/max | 26/33.92/162 ms | 26/33/40 ms |
| pause mean/P95/max | 9.053/11.464/13.053 ms | 8.737/11.249/13.354 ms |
| concurrent mean/max | 3.312/207.915 ms | 1.514/4.207 ms |
| process CPU | 15.386% | 15.168% |
| Private Bytes | 1302.039 MiB | 1293.435 MiB |
| Working Set | 1089.173 MiB | 972.356 MiB |
| GC heap | 488.970 MiB | 457.850 MiB |
| CPU Performance | 122.490% | 122.537% |

相位对齐且双方都无 Full 的窗口中，Young pause、concurrent、CPU 和内存
没有系统性回退；P5 和局部 max 有小幅运行方差。当前决定保留 128 MiB。

完整证据：

`_build/novagc-gameplay-major-growth128-clean2-230s-20260726/EVIDENCE_SUMMARY.md`

### 17.6 全部目标维度最新状态

| 目标维度 | 当前状态 | 当前结论 |
|---|---|---|
| 512 MiB SoftMax Full 风暴 | 已修复 | `soft_max` Full 10→0 |
| cycle 136 低收益 Major | 已修复当前场景 | 64→128 MiB 后停在 `cycle_growth_wait` |
| 全局 worker=1 | 已否决 | 未覆盖 Major，吞吐/P99 无收益 |
| Major STW | 已排除 | 207.915 ms 是并发工作，不是同长度 STW |
| Major 并发竞争 | 当前场景已规避 | 不再启动该低收益 cadence Major |
| Young pause | 无系统性回退 | 共同窗口 mean/P95 基本同级并略改善 |
| GC/进程内存 | 无回退 | 复测 heap/private/working set 均未增加 |
| 平均 FPS | 基本同级、略升 | GPU 计数器差异，不做独占归因 |
| P95/P99 帧稳定性 | 干净复测同级 | update 26/32、draw 26/33 ms |
| 极端 GC 邻近长帧 | 明显改善 | update/draw max 200/162→43/40 ms |
| NVIDIA/OpenGL 长帧 | 已独立确认 | 与 GC 分开，第一轮异常不掩盖 |
| ResultsScreen 41 秒卡顿 | 已独立确认 | 应用层同步构建，不计入 ZGC |
| 错误/崩溃/stall/fallback | 通过 | 两轮 8/8 均为 0 |
| 构建策略 | 通过 | 1/2774，逻辑 40/40 |
| 恢复点 | 完成 | 可精确回退 128 MiB 单变量 |
| 空间清理 | 被执行策略拦截 | 4 个候选 8.137 GiB 均未部分删除 |
| 最终目标 | 未完成 | 静态门禁仍需演进为成本/压力自适应 |

## 18. 长期计划与后续执行顺序

1. 保留已经通过回归的 `storeBarrier`、`PinnedBytes`、SoftMax 门禁和默认
   128 MiB Major 收益下限，不重复修改已稳定区域；
2. 增加自动 Full 历史：回收字节、回收比例、每毫秒回收字节、
   mark/relocation wall time 和对邻近帧的影响；
3. 使用 allocation rate、SoftMax headroom、预测耗尽时间和 CPU headroom
   计算 Major deadline，逐步替代静态 128 MiB；
4. 如需减少并发 worker，增加 Major 专用动态 worker，禁止再用会同时改变
   Young 的全局 2→1 开关；
5. 单独处理 NVIDIA/OpenGL 周期性离核和 ResultsScreen 同步图表构建，
   不把应用层/驱动收益记入 ZGC；
6. 单独处理启动 cycle 8/9 与 ResultsScreen 后 cycle 150/151 大 cohort；
7. 每个风险修改前建立精确恢复点，先跑目标测试，再跑逻辑 40/40，最后
   只做一次必要游戏增量构建；公共 ABI/PCH 不变时禁止全量重编；
8. 每轮正式验证至少 180 秒，并用 Haxe state/歌曲阶段确认同场景；
9. 强制抓取 Haxe 日志、Haxe 异常栈、原生线程栈、堆/GC、帧率、
   CPU/内存/线程、CPU Performance、GPU、窗口状态、Windows 事件和
   hang dump 状态；
10. 分开报告 ZGC、OpenFL/OpenGL、驱动和硬件状态，不用单轮高 FPS
    替代 GC 因果证据；
11. 删除前先验证路径严格位于 `_build`，只清理已提取摘要且可再生成的
    ETL/ETLX/NGENPDB，保留 CSV、JSONL、日志、摘要、恢复点和增量缓存；
12. 达到回退条件时只回退当轮单变量，不破坏已通过的正确性修复。

更完整的执行历史和实验细节位于：
`docs/HXCPP_ZGC_PERFORMANCE_OPTIMIZATION_EXECUTION_PLAN_2026-07-23.md`。
