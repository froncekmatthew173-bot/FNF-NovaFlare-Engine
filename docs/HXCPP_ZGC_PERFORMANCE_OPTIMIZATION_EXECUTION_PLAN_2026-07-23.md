# hxcpp NovaGC/ZGC 性能优化完整执行计划

> 日期：2026-07-23  
> 项目：NovaFlare Engine  
> 目标平台：Windows / hxcpp  
> 对照组：项目原有 Generational Immix、OpenJDK 26 Generational ZGC  
> 文档性质：后续实施、验收和回退的唯一主计划，不代表下列优化已经完成

## 1. 最终目标

本计划要解决的不是单次 GC 日志是否好看，而是 NovaGC/ZGC 在 NovaFlare 实际游戏负载下的综合表现：

1. 缩小 NovaGC 与项目原有 Immix 的主线程 Update 吞吐差距。
2. 保留移动式、精确、分代 GC 的正确性，不使用永久关闭移动或永久关闭自动 GC 作为最终方案。
3. 把 Young GC 的长尾暂停从当前最高约 58 ms 压低到不会明显打断菜单和游戏帧的范围。
4. 降低对象头、显式根、读写屏障和 pin 造成的额外内存与缓存压力。
5. 让 Haxe 生成器、hxcpp ABI、GC runtime、CFFI/native 边界形成一致且可验证的协议。
6. 建立与 JDK Generational ZGC 可逐项对照的阶段、屏障、根处理和调度模型。
7. 每一个优化阶段都必须有独立开关、A/B 数据、正确性测试和可恢复路径。

## 2. 当前结论与基线

### 2.1 当前构建选择

`Project.xml` 当前在 Windows 上启用：

- `HXCPP_GC_GENERATIONAL`
- `hxcpp_zgc`
- `novagc-precise`

定义 `legacy_gc_compare` 时回到项目原有 hxcpp Generational Immix，用作严格对照组。

### 2.2 现有性能数据

仓库已有样本去掉前 5 秒预热后，最接近的一组结果为：

| 指标 | Immix | NovaGC/ZGC | 当前差异 |
|---|---:|---:|---:|
| Update 吞吐 | 1653.5 | 979.1 | NovaGC 低约 40.8% |
| Draw 吞吐 | 697.6 | 703.5 | 基本相同 |
| CPU | 23.93% | 21.99% | 同一数量级 |
| Private Bytes | 491.5 MB | 697.4 MB | NovaGC 高约 41.9% |
| Working Set | 357.3 MB | 555.8 MB | NovaGC 高约 55.6% |
| Virtual Memory | 5.35 GB | 15.02 GB | NovaGC 约 2.81 倍 |

这些旧样本不是完全相同提交和完全相同构建参数下的最终科学基准，只能用于确定方向。正式优化开始前必须重新建立受控基线。

### 2.3 已确认的主要开销

1. `ObjectHeader` 当前固定为 64 字节，而 Immix nursery 常规路径只在对象前写入一个 4 字节分配头，其他信息主要由块级元数据承担。
2. NovaGC 的 `ObjectPtr` 读取快速路径仍然包含 slot acquire、全局 barrier state acquire 和 view 比较。
3. 当前生成目录约有 48,945 处 `RootedValue`、48,659 处 `HX_NOVAGC_VARI` 和 1,471 处 `ScopedPin`。
4. ETW 叶子样本中 `Runtime::loadBarrier` 聚合约 973 个样本，`Runtime::instance()` 约 372 个样本，根析构、pin 和 remembered-set 路径也进入了热点。
5. Young GC 只把主体 mark 放到并发阶段；final remark、弱引用/ephemeron、evacuation、remap 和部分 finalizer 工作仍然位于第二次 STW 区间。
6. 已有 Young 日志多数暂停约 4～14 ms，但出现过 22.509 ms 和 58.405 ms 长尾。
7. 默认策略以 16 MiB 初始/Young 触发、2 个 mark worker、2 个 relocation worker、10 ms pause target 起步，反馈模型仍明显简单于 HotSpot ZGC。

## 3. 不接受的“优化”方式

以下方式可以作为诊断开关，但不能作为完成方案：

- 永久设置 `HXCPP_NOVAGC_YOUNG_NO_MOVE=1`。
- 永久关闭 `HXCPP_NOVAGC_AUTO_GC` 或 Young GC。
- 只扩大堆或 Young 区，让问题更晚出现。
- 只提高 worker 数量，却不处理每次引用访问和每个局部根的常态成本。
- 通过删除 safepoint、根或写屏障换取跑分。
- 只验证 title/menu，不验证 PlayState、音频、视频、CFFI、脚本和线程路径。
- 用不同提交、不同资源、不同窗口状态或不同编译优化等级比较 Immix 与 NovaGC。

## 4. 总体实施顺序

实施顺序按“先控制变量，再减少 mutator 税，最后重构并发阶段”安排：

1. 冻结基线和自动化测试协议。
2. 建立生成代码根/屏障数量统计和 runtime 事件统计。
3. 合并函数级精确根，减少 `RootScope` 与 `RootedValue`。
4. 优化读写屏障和 remembered-set 热路径。
5. 缩小对象头并把可旁路的数据移入 region/page 元数据。
6. 重构 Young GC 为短暂停加并发 relocation/remap。
7. 重做 pin/native/CFFI 地址稳定协议。
8. 引入动态 worker、Young 大小和 tenuring 策略。
9. 进行完整正确性、压力、性能和回退验收。

任何阶段没有通过正确性门槛，不允许继续叠加下一阶段优化。

## 5. 阶段 0：冻结受控基线

### 5.1 构建矩阵

必须从同一提交、同一工作树、同一资源和同一编译优化等级生成：

| 构建 | 定义 | 用途 |
|---|---|---|
| NovaGC 默认 | `hxcpp_zgc` + `novagc-precise` | 主实验组 |
| Immix 对照 | `legacy_gc_compare` | 吞吐和内存基线 |
| NovaGC no-move | `HXCPP_NOVAGC_YOUNG_NO_MOVE=1` | 只用于定位 relocation 正确性/成本 |
| NovaGC 无自动 GC | `HXCPP_NOVAGC_AUTO_GC=0` | 只用于区分 mutator 常态成本和 collection 成本 |

默认构建命令使用仓库工具链：

```powershell
& tools/build-novagc-windows.ps1 -Command build
```

最终交付验证必须包含：

```powershell
& tools/build-novagc-windows.ps1 -Command test
```

如果 `lime test windows` 构建成功但启动器失败，要单独记录启动器错误，并手动运行生成的应用二进制确认实际程序行为，不能把两者混为一谈。

### 5.2 固定场景

每个构建至少采集以下场景：

1. Title 静置。
2. Main Menu 静置。
3. Freeplay 列表静置与连续滚动。
4. 普通原版歌曲 gameplay。
5. 高 Lua/HScript 事件密度歌曲。
6. 大贴图/角色切换场景。
7. 视频播放、结束和再次播放。
8. State 切换与 `Paths.clearUnusedMemory()`。
9. 30 分钟持续运行。

每个场景至少预热 10 秒，正式采集 60 秒，重复 5 次，使用中位数和 P95/P99，不能只保留最好的一次。

### 5.3 必采指标

- Update FPS/TPS、Draw FPS。
- Update/Draw frame time 的 P50、P95、P99、最大值。
- 进程 CPU、Private Bytes、Working Set、Commit、Virtual Bytes。
- 每秒分配字节、对象数、TLAB fast/slow 次数。
- load/store barrier 次数和 slow-path 比率。
- RootFrame/RootScope/RootedValue/ScopedPin 构造次数。
- Young/Full cycle 次数。
- initial pause、final pause、concurrent mark、evacuate、remap、finalizer 时间。
- promoted/survivor/in-place-promoted region 数量。
- remembered slots、overflow region、pin region 和 relocation failure 数量。

### 5.4 阶段完成条件

- Immix 和 NovaGC 的构建与运行条件完全一致。
- 自动化脚本能重复生成 CSV、GC 日志和截图。
- 所有原始数据保留，聚合脚本不能覆盖原始样本。
- 基线报告明确区分 mutator 常态成本、并发 GC CPU 和 STW 暂停。

## 6. 阶段 1：重做生成代码的精确根布局

这是最高优先级，因为当前近十万处显式根包装会在没有发生 GC 的普通帧里持续收费。

### 6.1 目标设计

把“每个局部变量一个 `RootScope`”改为“每个函数一个紧凑 `RootFrame`”：

```text
函数入口
  -> 一次登记 RootFrame
  -> frame 内保存连续 RefSlot 数组或编译期已知 offset 表
  -> 局部变量只占用 slot，不各自构造/析构 RootScope
函数出口
  -> 一次注销 RootFrame
```

### 6.2 具体任务

1. 修改 `toolchains/haxe-novagc/src/generators/gencpp.ml`，在函数级收集需要跨 safepoint 存活的对象局部变量。
2. 区分以下生命周期：
   - 函数参数。
   - 普通局部变量。
   - 调用参数求值期间的临时值。
   - 循环体跨 safepoint 值。
   - 异常路径和闭包捕获值。
3. 为函数生成固定 slot 数量和静态描述信息。
4. 对不跨分配、不跨调用、不跨 safepoint 的临时对象取消根登记。
5. 保留多参数 C++ 求值顺序安全，不能因为压缩根而重新引入调用参数悬空。
6. 将 `RootedValue<ObjectPtr<T>>` 的 receiver pin 改为更小的受控生命周期。
7. 评估普通 Haxe 方法调用能否依赖：
   - 入口 receiver root；
   - relocation load barrier；
   - 或只在确实暴露原始地址时 pin。
8. 为异常、return、break、continue 和早退生成统一 frame 清理。
9. 保留旧根 ABI 开关，允许逐文件 A/B 和快速回退。

### 6.3 正确性验证

- 多参数调用中每个参数都能触发分配/GC。
- 构造函数嵌套分配。
- 闭包捕获局部变量。
- try/catch/finally 和异常展开。
- 数组、Dynamic、String、interface cast。
- HScript/Iris 调用。
- native callback 在 GC 前后保留正确对象。

### 6.4 性能门槛

- 生成 C++ 中独立 `RootScope`/`RootedValue` 数量至少降低 80%。
- RootScope 构造/析构不再进入主要 ETW 叶子热点。
- 相同 GC 频率下 Update 吞吐有可重复提升。
- 不允许增加漏根、错误 pin 或随机 native handle 失效。

## 7. 阶段 2：读写屏障与 remembered-set 热路径

### 7.1 读取屏障

当前 `loadBarrierFast` 每次对象读取都会加载 slot 和全局状态。计划分层处理：

1. 把当前 view/phase 缓存到线程本地 mutator state。
2. 只在 collector 改变全局 epoch 时刷新线程缓存。
3. 对编译器已证明处于新分配对象、非移动阶段或同一表达式内的重复读取做屏障合并。
4. 支持同一引用在基本块内的 barrier domination，避免连续多次 heal/check。
5. slow path 继续完成 relocation/remap/self-heal，不能牺牲移动正确性。
6. 记录每类 slow-path 原因，而不是只有总次数。

### 7.2 写屏障

1. 生成器尽量传入精确 owner，避免 `owner == null` 后通过 slot 地址反查对象。
2. 区分：
   - 栈根写入。
   - 静态根写入。
   - 新对象初始化写入。
   - young-to-young。
   - old-to-young。
   - relocation 期间写入。
3. 新对象尚未发布前的字段初始化走无 remembered-set 快路径。
4. old-to-young 记录使用线程本地 buffer，满时批量发布。
5. 使用 region/card/bitmap 的 act-once 语义，减少 `unordered_map` 插入和锁竞争。
6. overflow 必须保留可验证的整区扫描降级路径。

### 7.3 对照 JDK ZGC

目标不是逐行复制 HotSpot，而是实现相同原则：

- 正常 colored reference 只走极短测试。
- bad reference 才进入 slow path。
- slow path 完成 self-heal。
- store barrier 优先进入线程本地 buffer。
- phase/view 改变通过 patch/epoch 机制统一生效。

### 7.4 阶段完成条件

- `Runtime::loadBarrier` 不再是第一梯队叶子热点，或样本显著下降。
- owner-less heap store 比例接近零。
- remembered-set 锁等待和哈希分配显著下降。
- barrier 优化开启/关闭可用同一测试矩阵对照。

## 8. 阶段 3：缩小对象头与分配快速路径

### 8.1 对象头拆分

当前 64 字节对象头中的信息按使用频率拆分：

| 字段类型 | 计划位置 |
|---|---|
| 类型和对象大小 | 保留最小内联信息或紧凑编码 |
| mark/liveness | page/region side bitmap |
| forwarding | relocation set 专用 side table |
| region pointer/index | 通过地址范围和 region table 推导 |
| pin count | pinned side table 或稀疏表 |
| identity | 只在首次需要 identity 时懒分配 |
| age | page age 或紧凑对象年龄表 |

第一阶段目标是从 64 字节降到不超过 32 字节；是否继续降到 16 字节必须以复杂度、缓存和访问成本实测决定。

### 8.2 TLAB 分配

1. TLAB fast path只执行对齐、cursor bump 和必要最小头初始化。
2. identity 不在每个对象创建时无条件分配。
3. object-start bitmap 采用线程本地批量发布或按 TLAB 一次发布。
4. 分配计数继续批量 flush，避免每对象原子操作。
5. TLAB 大小根据分配速率和浪费率动态调整。
6. large object 和 pinned object 使用独立 page/region class。

### 8.3 阶段完成条件

- 小对象实际占用明显下降。
- 同样有效载荷下 Young region 数量下降。
- Private Bytes 与 Working Set 相对 Immix 的差距降至可解释范围。
- allocation-only microbenchmark 的 fast-path 成本显著接近 Immix nursery。

## 9. 阶段 4：真正并发的 Young relocation/remap

### 9.1 目标阶段模型

参考 OpenJDK 26 Generational ZGC，将 Young cycle 拆为：

1. `Pause Mark Start`：切换 epoch/view，快照必要根，立即恢复 mutator。
2. `Concurrent Mark`：并发追踪 Young。
3. `Pause Mark End`：完成有限 closure 和 relocation set 发布。
4. `Concurrent Mark Free/Reset`：清理死亡对象元数据。
5. `Concurrent Select Relocation Set`：计算转移集合和目标容量。
6. `Pause Relocate Start`：切换 load barrier 状态并发布 forwarding 协议。
7. `Concurrent Relocate`：并发复制，mutator 通过 load barrier 协助或等待单对象转移。
8. `Concurrent Root Remap`：通过栈水位或分批 handshake 修复根。
9. 并发回收旧 region；仅在所有线程确认不再持有旧地址后复用。

### 9.2 必须解决的问题

- C++ 原始 receiver 指针不能跨 relocation safepoint 裸奔。
- 栈根必须拥有可增量处理的 frame/stack map。
- forwarding 读写必须有明确状态机，防止重复复制。
- mutator allocation、collector copy 和 promotion reserve 不能互相耗尽空间。
- weak、soft、ephemeron、finalizer 顺序必须与可达性协议一致。
- native 线程进入/退出 GC-free zone 必须参与 epoch 握手。
- relocation 失败必须安全降级为 in-place，而不是部分提交后回滚。

### 9.3 阶段完成条件

- evacuation 和大部分 remap 不再计入 final STW pause。
- 10 ms pause target 在常规场景的 P99 内真实成立。
- 不再出现当前 22～58 ms 的 Young final pause 长尾。
- 并发 relocation 开启后没有新增 CFFI、渲染、音频或回调地址失效。

## 10. 阶段 5：pin、native 与 CFFI 协议

### 10.1 pin 分类

必须区分：

- 只保证可达性的 strong handle。
- 需要稳定地址的 native handle。
- 单个调用表达式内的短 pin。
- 长时间由 C 库保存的 callback context。
- 大型 Bytes/Bitmap/Audio buffer。

### 10.2 实施任务

1. 普通 Haxe 方法 receiver 不应默认变成长 pin。
2. CFFI 只拿 opaque stable handle，不直接长期保存可移动 Haxe 对象地址。
3. 长生命周期 pinned 对象进入专用 pinned region/page。
4. 统计每个类型、调用点、region 的 pin 数量和持续时间。
5. region relocation selection 根据 pin density 排除区域，而不是在最后阶段被动失败。
6. Native callback attach/callback/detach 必须使用同一个保留指针。
7. handle 释放必须幂等，并在最后一个 native 使用者退出后发生。

### 10.3 阶段完成条件

- `in_place_promoted_regions` 在普通菜单和 gameplay 中接近零。
- 能定位所有长 pin 的类型和调用点。
- 视频、音频、BitmapData、OpenGL buffer 和线程回调压力测试通过。

## 11. 阶段 6：动态调度与分代策略

### 11.1 Young 大小

根据以下信号动态决定下一轮 Young headroom：

- 最近分配速率。
- Young 存活率和垃圾回收收益。
- concurrent mark/relocate 完成速度。
- promotion debt。
- pause P95/P99。
- soft max 和物理内存压力。

### 11.2 Tenuring

参考 JDK Generational ZGC 的思路，综合：

- 各 age cohort 的存活衰减。
- 当前 Young live bytes。
- allocated-to-garbage 比例。
- Old residency。
- promotion reserve。

避免只在 2～3 之间简单切换导致短命对象过早进入 Old。

### 11.3 Worker

1. 初始 worker 数根据可用 CPU 和 mutator 数确定。
2. 按队列积压、分配压力和 pause miss 动态扩缩。
3. mark、ref processing、relocation 使用可工作窃取队列。
4. 避免 GC worker 抢占主游戏线程导致 Update 抖动。
5. 记录 worker utilization，不能只记录配置数量。

### 11.4 阶段完成条件

- 常规场景不再高频按固定 8～16 MiB 节奏触发 Young。
- 高存活率场景能自动提高晋升或扩大 Young，不产生 promotion storm。
- worker 增加后总 wall time 下降，而不是仅提高 CPU 占用。

## 12. 阶段 7：可观测性和诊断工具

### 12.1 统一日志

每个 cycle 输出结构化 JSON/CSV：

- cycle/epoch/generation/reason。
- allocation、used-before/after、live、garbage。
- root/remembered/pin 数量。
- 各阶段 wall/cpu 时间。
- worker utilization。
- relocation set、copy、in-place、failure。
- pause history 和目标偏差。

### 12.2 类型级数据

- 每种类型的分配对象数/字节。
- nursery 死亡数量。
- survivor/promotion 数量。
- 平均对象大小。
- 引起 pin 或 finalizer 的数量。

类型 census 必须在 mutator 恢复后执行，不能为了诊断扩大 STW。

### 12.3 自动化报告

报告必须同时显示：

- Immix 与 NovaGC 的绝对数值。
- 相对差异。
- 五次运行的离散程度。
- GC 开启与无自动 GC 的差异。
- no-move 与 moving 的差异。
- 优化前后 ETW 顶部符号变化。

## 13. 正确性测试矩阵

### 13.1 GC 核心

- TLAB refill。
- Young survivor 和 promotion。
- Full concurrent mark/relocate。
- weak/soft reference。
- ephemeron fixed point。
- finalizer 重入和分配。
- identity/hash 稳定。
- pinned object。
- large object。
- remembered-set overflow。
- allocation failure 和 reserve exhaustion。
- 多线程 attach/detach。

### 13.2 Haxe/hxcpp 语言特性

- Dynamic 字段。
- interface、cast、reflection。
- Array、Vector、String、Bytes。
- closure、异常、递归。
- enum、anonymous structure。
- static root、lazy literal。
- HScript/Iris 动态调用。

### 13.3 引擎集成

- OpenFL TextField/BitmapData。
- Flixel sprite/group/camera。
- Lime audio tracks。
- OpenGL texture/buffer。
- hxvlc attach/callback/detach。
- Discord/native extensions。
- 后台资源线程和主线程回调。
- Lua/HScript 高频事件。

## 14. 性能验收门槛

最终完成必须同时满足以下条件，不能只满足其中一个：

| 项目 | 最低验收目标 |
|---|---|
| Update 吞吐 | 与同条件 Immix 差距缩小到 15% 以内；目标 10% 以内 |
| Draw 吞吐 | 不低于基线 5% 以上 |
| Working Set | 相对 Immix 的额外占用控制在 25% 以内，并给出来源 |
| Young pause P99 | 常规场景不超过 10 ms |
| Young pause max | 受控测试中不再出现 50 ms 级长尾 |
| Root 包装 | 独立 RootScope/RootedValue 降低至少 80% |
| loadBarrier 热点 | ETW 样本显著下降，不再长期位于首要热点 |
| 正确性 | 30 分钟压力运行无悬空、handle 清除、错误 finalizer 或 native crash |
| 可回退性 | 每个大阶段能通过单独编译开关退回上一稳定实现 |

如果最终吞吐仍低于 Immix，但 pause 和大堆行为明显更好，报告必须明确写出这是一项延迟/内存权衡，不能宣称“全面更快”。

## 15. 回退和风险控制

### 15.1 每阶段要求

- 开始前记录工作树和构建标识。
- 修改生成器前保留生成代码样本。
- 修改对象布局时提升 ABI/version 标识，禁止新旧对象混用。
- 修改 barrier 或 relocation 时保留旧路径编译开关。
- 任何 native crash 先保存日志、cycle 和类型统计，再回退当前阶段。

### 15.2 立即回退条件

- 同一指针在 attach/detach 中发生变化。
- CFFI strong handle 被意外清空。
- 对象字段通过屏障后仍指向 retired region。
- root snapshot 缺槽或出现未发布对象。
- partial relocation 后无法确定对象唯一新地址。
- finalizer 对仍可达对象执行。
- 构建产物混入 legacy GC ABI。

## 16. 首批实际改动清单

第一批只处理根和测量，不同时重写对象头或 concurrent relocation：

1. 在生成器中统计每个函数的对象参数、局部根、临时根、receiver pin。
2. 在构建结束输出全项目汇总，固定当前约 9.7 万处显式根包装基线。
3. 新增函数级 `RootFrame` ABI，但先通过编译开关启用。
4. 选择小型独立模块做生成器试点，不直接全项目切换。
5. 运行 hxcpp NovaGC smoke/unit/probe matrix。
6. 生成并人工审查试点模块的 C++。
7. 扩展到 Flixel/OpenFL 高频类。
8. 重新采集 ETW、Update 吞吐和 root 构造次数。
9. 只有正确性和性能同时通过，才将函数级 RootFrame 设为默认。

## 17. 最终交付物

完成本计划时必须交付：

1. 生成器和 runtime 源码改动。
2. ABI/屏障/根/relocation 协议文档。
3. 自动化构建与 A/B 基准脚本。
4. 原始 CSV、JSON、GC 日志和 ETW 采样。
5. Immix、NovaGC 优化前、NovaGC 优化后的对比报告。
6. 正确性测试矩阵结果。
7. Windows `lime test windows` 结果及手动二进制运行结果。
8. 已知限制和未完成项，禁止用“基本完成”掩盖关键缺口。
9. 每个阶段的回退开关和使用方法。

## 18. 当前状态

- [x] 已定位 NovaGC 相对 Immix 的主要性能来源。
- [x] 已使用仓库内 OpenJDK 26 ZGC 源码完成架构对照。
- [x] 已确认现有差距主要位于 Update/mutator 路径，而不是 Draw。
- [x] 已确认 64 字节对象头、引用屏障、显式根/pin 和 Young final STW 是首要问题。
- [ ] 尚未冻结新的严格同提交 A/B 基线。
- [ ] 尚未实现函数级 RootFrame。
- [ ] 尚未实现编译期 barrier elimination。
- [ ] 尚未缩小对象头。
- [ ] 尚未完成 concurrent relocation/root remap。
- [ ] 尚未完成动态分代调度和最终验收。

本文件之后的实施应按阶段更新复选框、实测数据和偏差说明。任何性能数字必须能追溯到对应构建、命令、日志和原始样本。

## 19. 2026-07-23 实际执行摘要

本节记录本轮已经执行的工作，不再只是计划。

### 19.1 备份与回退点

- 执行前恢复点：`_build/restore-before-zgc-performance-execution-20260723-084257`。
- 恢复点包含修改前的 `Project.xml`、NovaGC runtime/header、生成器、文档、工具以及 `.haxelib/hxcpp/.dev` 指向信息。
- 关键文件在备份时做了 SHA-256 校验。
- 恢复点没有在后续空间清理中删除。
- 当前生产构建缓存 `export/release/windows/obj` 也保留，后续仍可增量编译。

### 19.2 空间清理

- 第一轮删除已验证为旧备份、旧调试构建、过期 trace 和可重建缓存，释放约 12.89 GiB。
- 最后一轮删除原始 CPU/GPU ETL、ETLX、符号专用 EXE/map、重复 legacy 静态对象，释放 5.542 GiB。
- 总计释放约 18.43 GiB。
- 清理完成后 D 盘可用空间约 35.23 GiB。
- 保留了小体积 CSV、截图、热点解析表、最终 EXE、当前增量缓存和唯一恢复点。
- 最终清理脚本：`_build/perf-evidence-retained-20260723/cleanup-final-large-artifacts.ps1`。

### 19.3 本轮明确未改动的范围

- 没有修改 Freeplay、BPM 显示或 Freeplay 菜单逻辑。
- 没有修改对象布局，因此没有引入新的对象 ABI。
- 没有在本轮重写生成器、Young relocation 或 native/CFFI 协议。
- 没有把放大 Young 区、关闭 GC 或关闭移动作为默认方案。

## 20. “突然卡到 2 帧”的直接根因

### 20.1 现场证据

已确认主菜单的故障样本位于：

`_build/default-young-main-csv90b-20260723`

该样本中：

- 完整 CSV 记录了 549 次 Young 和 510 次 Full。
- 稳态 60 秒窗口 Update 平均只有 2.006 TPS，P01 为 2，最低为 1。
- 同一窗口 Draw 仍有平均 239.556 FPS，说明渲染线程没有同幅度停摆，主要被饿死的是 update/mutator 路径。
- Young 暂停平均 45.330 ms，P99 94.945 ms，最大 278.416 ms。
- Full 暂停平均 39.761 ms，P99 97.154 ms，最大 129.043 ms。
- committed 平均约 598.845 MiB、最大约 795.066 MiB；实际 used 并没有与 committed 同步保持在阈值之上。

### 20.2 错误调度链

旧代码使用以下条件判断 soft max：

```cpp
runtime.committedBytes() > automaticSoftMaxBytes
```

默认 soft max 是 512 MiB。问题在于 committed region 是已经向系统提交、可继续复用的容量，并不等于仍然存活的对象。一次 Full 即使回收了大量对象，也不保证立刻把 committed 降到 512 MiB 以下。

因此旧调度会形成：

```text
Young -> committed 仍大于 512 MiB -> Full
      -> committed 仍大于 512 MiB -> 下一次 Young 后再次 Full
      -> 持续循环，update/mutator 被 GC 连续占用
```

这就是本次 2 TPS 的直接原因，不是 GPU 性能突然下降，也不是简单地把 Young 区调大就能可靠解决的问题。

### 20.3 同时观察到的渲染异常

故障进程退出日志中还曾重复出现：

```text
Null Object Reference
openfl/display/Graphics.beginShaderFill()
flixel/graphics/tile/FlxDrawQuadsItem.render()
```

后续多轮默认配置与受控阈值运行都没有再次产生该异常，stdout 只有 15 行正常启动/内存清理日志，stderr 为空。因此本轮没有在缺少稳定复现的情况下修改 OpenFL/Flixel 渲染代码。该异常保留为独立观察项，不能把它与已经定量确认的 Full GC 风暴混为同一根因。

## 21. 本轮已实现的代码改动

### 21.1 引用复制与空引用快路径

文件：`hxcpp/include/hx/gc/runtime.hpp`

- `makeRefFast(nullptr)` 在读取全局 barrier state 前直接返回空引用。
- 新增 `copyBarrierFast(owner, slot, source)`。
- GC 空闲或目标是外部根 slot 时，复制已着色引用只走一次快速复制。
- 活跃标记/迁移或 managed arena 写入仍回退到完整 load barrier + store barrier，保留 self-heal 和 remembered-set 正确性。

文件：`hxcpp/include/hxcpp.h`

- `ObjectPtr` 同类型复制、移动和可转换模板路径使用 `copyBarrierFast`。
- runtime checked cast 只求值一次 `other.get()`，避免重复屏障。

### 21.2 RootScope 安全 LIFO 快路径

文件：`hxcpp/include/hx/gc/runtime.hpp`

- 已 attach 的正常线程在根 frame 严格 LIFO 时，构造/析构不再无条件进入较重的通用路径。
- 非 LIFO、未 attach 或状态不满足条件时仍使用原有安全慢路径。

### 21.3 soft-max Full GC 风暴修复

文件：`hxcpp/runtime/core/runtime_facade.cpp`

已完成两项修改：

1. soft max 从 `committedBytes()` 改为 Young 结束后的 `young.usedAfter`，按实际使用量而不是可复用提交容量判断。
2. 如果 Full 后实际使用量仍高于 soft max，则必须再分配至少 `automaticFullPromotionBytes`（默认 512 MiB）才允许 soft-max 原因再次触发 Full。

显式 Full 请求也会更新同一冷却状态，避免手动 Full 后立刻再被 soft max 补一次 Full。

### 21.4 Immix 对照构建隔离

`Project.xml` 为 `legacy_gc_compare` 增加独立的 `export/legacy-gc` 构建目录，使 NovaGC 和 legacy Immix 不再互相覆盖对象缓存。

## 22. 构建与正确性验证

### 22.1 原生测试

引用/根快路径修改后通过以下 10 项测试：

- hxcpp thread
- thread shutdown
- compatibility
- static roots
- pinned space
- in-place relocation
- relocation
- concurrent relocation
- relocation failure
- late pin

结果：10/10 通过，0 失败。

soft-max 调度修改后又针对性重编并通过：

- `hxcpp-gc-automatic-test`
- `hxcpp-gc-uncommit-test`
- `hxcpp-gc-young-test`

### 22.2 游戏构建

- header 快路径修改需要全量重编，已一次性完成 2774/2774 个翻译单元。
- soft-max 修改只涉及 `runtime_facade.cpp`，随后构建为 1/2774 个翻译单元增量编译并重新链接。
- 两次构建均通过 NovaGC precise ABI verification。
- 当前 Windows EXE SHA-256：`A25C6943EBD83AFE93948B805B8BCD9963702993D3FBDA045F2E091FA54D395C`。

### 22.3 legacy Immix 对照限制

严格 legacy 构建已生成：

`export/legacy-gc/windows/bin/NovaFlare Engine.exe`

SHA-256：`E8AC6BCB85889B88B79717ED51271CF96B57BB2573137B886D00B9202BD09BC1`。

该构建在当前工作树启动时发生 `hxSehException`，CDB 定位为 `ApplicationMain!vsnprintf+0x315ec0` 附近的空地址读取。因为对照组无法进入相同运行场景，本轮没有伪造新的“严格同提交”Immix 数字，仍将已有可运行 Immix 样本作为方向参考。

## 23. 修复后的实测结果

### 23.1 默认 512 MiB soft max

有效主菜单样本：

- `_build/fixed-softmax-main-default-180s-cross-threshold-20260723`
- `_build/fixed-softmax-main-default-270s-full-proof-20260723`

第一份样本的稳态末 60 秒：

| 指标 | 修复后 |
|---|---:|
| Update 平均 | 355.996 TPS |
| Update P01 | 40 TPS |
| Draw 平均 | 409.171 FPS |
| Draw P01 | 363.55 FPS |
| CPU 平均 | 17.151% |
| Private Bytes 平均 | 775.124 MiB |
| Working Set 平均 | 692.329 MiB |
| Young / Full | 47 / 0 |

另一轮较长默认配置样本记录 38 次 Young、0 次 Full；used-after 最大约 417.604 MiB、committed 最大约 484.605 MiB。它没有跨过默认 512 MiB 实际使用量，因此只证明正常负载不再被 committed 容量误判，不能用来声称完成了默认阈值跨越测试。

需要注意：修复后 Young 暂停仍存在长尾。第一份样本 Young 平均约 11.522 ms、P99 约 75.915 ms、最大约 88.523 ms；第二份样本平均约 8.694 ms、P99 约 35.651 ms、最大约 38.009 ms。2 TPS 风暴已经修复，但 Young final STW 长尾还没有达到最终验收目标。

### 23.2 384 MiB 受控阈值验证

为了强制进入 soft-max 分支，只在测试进程中设置：

```text
HXCPP_NOVAGC_SOFT_MAX_HEAP_MB=384
```

证据目录：`_build/fixed-softmax-main-384m-180s-proof-20260723`。该运行被交互停止指令截短，不能当作完整 180 秒帧率样本，但 GC 分支证据完整：

- 共记录 17 次 Young、1 次 Full。
- 第 14 次 Young committed 已约 406.5 MiB，旧代码会从这里开始触发 Full；新代码没有触发。
- 第 17 次 Young 后实际 used 达到约 392.4 MiB，超过 384 MiB，才触发一次 Full。
- 该 Full 将 used 从约 392.4 MiB 降到约 248.6 MiB。
- Full pause 约 69.263 ms。

这个受控结果证明新条件区分了 committed capacity 与实际 heap usage，已切断旧的“只要 committed 高就每次 Young 后 Full”链条。

### 23.3 大 Young 区实验为什么没有作为默认值

曾测试 32 MiB 初始/最小 Young、128 MiB 最大 Young、12 ms pause target。自适应 headroom 从 32 MiB 逐步升到 97 MiB，Young 次数明显减少，但进程内存增长更快，并更容易接近 soft max。该方案只是在频率和单次成本之间换挡，不能替代 soft-max 修复，也不能解决 64 字节对象头、显式根和屏障税，因此没有写入默认配置。

## 24. ETW/堆栈热点与 JDK ZGC 对照结论

### 24.1 当前热点

当前 60 秒 CPU stack 的主要 NovaGC 叶子样本包括：

| 符号 | 样本数 |
|---|---:|
| `Runtime::typeOf` | 1503 |
| `RefSlot::load` | 868 + 114 |
| `RootScope::~RootScope` | 428 + 200 + 53 + 49 |
| `RootScope::RootScope` | 343 + 190 + 169 |
| `Runtime::storeBarrier` | 307 + 254 |
| `Runtime::loadBarrier` | 284 |
| `Runtime::isInstanceOf` | 218 |
| `ScopedPin` 构造/析构 | 124 / 106 等 |

旧样本中 `loadBarrier` 聚合约 973 个样本，当前样本约 284，说明引用复制快路径已经把一部分成本移出完整 load barrier；但采样时长和工作负载并不完全相同，不能把 973 到 284 当成精确百分比加速。

热点已经明显转移到 `typeOf`、`RefSlot::load`、RootScope 和 store barrier。下一阶段应先处理生成器根生命周期、类型检查和 TLS barrier state，而不是继续在已经下降的单一 loadBarrier 函数上内耗。

### 24.2 为什么仍慢于 Immix

项目 NovaGC 相比 Immix 仍有以下结构性成本：

- 当前对象头固定 64 字节，Immix 常规分配路径只需要很小的对象前缀并把更多信息放在块元数据中。
- 每次普通引用访问仍可能经过带颜色引用、视图检查、load/store barrier 和 self-heal 协议。
- 生成代码中仍有约 48,945 个 `RootedValue`、48,659 个 `HX_NOVAGC_VARI` 和 1,471 个 `ScopedPin`。
- Young 的最终追踪、弱引用/ephemeron、evacuation/remap 仍有大块 STW 工作。
- hxcpp AOT 生成器目前没有 HotSpot C2 那种屏障消除、寄存器/栈图协同和 patchable barrier 能力。

### 24.3 与 OpenJDK 26 Generational ZGC 的差异

仓库内 OpenJDK 26 源码显示其 Young 周期按短暂停和并发阶段拆分：

- Pause Mark Start
- Concurrent Mark
- Pause Mark End
- Concurrent Mark Free
- Concurrent Reset/Select Relocation Set
- Pause Relocate Start
- Concurrent Relocate

HotSpot ZGC 的正常 colored pointer 屏障是编译器配合的极短路径，slow path 才做 remap/self-heal；store barrier 还使用线程本地缓冲。项目 NovaGC 目前是以正确性为先的 hxcpp AOT runtime，屏障、根、pin 和 Young final relocation 远没有做到同等程度的编译器协作，因此不能仅凭名称把它视为“JDK ZGC 的等价实现”。

## 25. 这批改动能解决什么、不能解决什么

### 已解决或明显改善

- 已修复导致主菜单突然跌到 2 TPS 的 Full GC 调度风暴。
- committed 超过 soft max 不再被误认为 live heap 超限。
- 同类 `ObjectPtr` 复制和空引用减少了不必要的完整 load barrier。
- RootScope 常规严格 LIFO 路径减少了部分通用管理成本。
- 默认主菜单运行不再出现一次 Young 紧跟一次 Full 的模式。
- 构建流程已经做到一次必要全量编译，后续调度修复只编译 1/2774 单元。

### 尚未解决

- NovaGC Update 吞吐仍不能宣布追平 Immix；严格同提交 Immix 对照当前还会启动崩溃。
- Young final STW 的 P99/最大长尾仍高于 10 ms 目标，可能继续造成偶发帧时间尖峰。
- 64 字节对象头造成的内存、cache 和分配带宽成本没有改变。
- 约 9.7 万处显式根包装及大量 `typeOf`/cast 路径仍是主线程税。
- 真正并发 Young relocation/root remap、TLS store buffer 和生成器 barrier elimination 尚未实现。
- 当前帧率监控器在后续二进制上有一次 RVA 自动解析到错误计数器、产生不可能的大数；相关异常帧率样本不纳入结论，GC CSV、进程计数器和已人工确认截图仍有效。

因此本轮结论是：**灾难性的 2 TPS 风暴已修复，常态屏障成本有所下降，但还不能声称 NovaGC 的综合性能已经达到 Immix 或 JDK ZGC。**

## 26. 保留证据与下一步优先级

### 26.1 保留证据

- CPU 热点：`_build/optimized-root-copy-main-stacktrace-20260723/cpu-hotspots-resolved.tsv`
- CPU 堆栈摘要：`_build/optimized-root-copy-main-stacktrace-20260723/cpu-stack-summary.txt`
- 故障样本：`_build/default-young-main-csv90b-20260723`
- 修复后默认样本：`_build/fixed-softmax-main-default-180s-cross-threshold-20260723`
- 修复后较长默认样本：`_build/fixed-softmax-main-default-270s-full-proof-20260723`
- 受控阈值样本：`_build/fixed-softmax-main-384m-180s-proof-20260723`
- 监控脚本：`_build/perf-evidence-retained-20260723/run-main-monitor.ps1`
- 汇总脚本：`_build/perf-evidence-retained-20260723/summarize-performance.ps1`
- 恢复点：`_build/restore-before-zgc-performance-execution-20260723-084257`

### 26.2 下一轮优先级

1. 修正帧率监控器的符号/RVA 自动定位，以符号或导出元数据代替宽范围内存猜测。
2. 在生成器中合并函数级根 frame，先消灭 `RootScope` 构造/析构热点。
3. 优化 `typeOf`/`isInstanceOf` 高频路径，避免重复读取屏障和类型元数据。
4. 把 barrier phase/view 缓存到 mutator TLS，并为 store barrier 增加线程本地缓冲。
5. 缩小对象头，改动前先提升对象 ABI 版本并建立旧构建拒绝机制。
6. 将 Young evacuation/remap 拆到并发阶段，专门压低 final pause P99。
7. 修复 strict legacy Immix 对照启动崩溃后，重新做同提交、同场景、同窗口状态的五轮 A/B。

## 27. 下一阶段硬目标：完整日志、栈与堆证据链

本阶段把“任何性能异常、2 FPS、崩溃或无响应都必须留下可分析证据”列为硬目标。一次正式采集至少必须包含：

1. Haxe 标准输出和 `trace` 日志；
2. Haxe 未捕获异常消息、格式化异常栈、原始异常栈和当前调用栈；
3. Haxe 异常时的 GC used、committed 和应用层内存读数；
4. NovaGC 每周期 CSV、JSON，以及不依赖控制台输出的类型 census/handshake 诊断日志；
5. 进程 CPU、工作集、Private Bytes、线程数、句柄数和响应状态时间序列；
6. 由进程内 `NOVAGC_PERF_TRACE` 产生的 Update/Draw FPS 与帧时间 CSV；
7. Windows Application Error / WER 事件；
8. 原生未处理异常的完整内存 dump 和异常码/地址文本；
9. 持续无响应时的进程完整 dump；
10. 从所有文本中抽取的 Haxe/C++ 错误、异常与 stack 关键行；
11. 带开始/结束时间、退出码、文件存在性、样本数和 dump 数的 manifest。
12. WPR CPU ETW 全程采样栈；若系统已有冲突会话或权限不足，manifest 必须明确记录不可用原因。

对应工具：

- `tools/run-novaflare-diagnostics.ps1`：统一启动、自动交互、持续采样、卡死判断、错误抽取和 manifest。
- `tools/capture-novaflare-process-dump.ps1`：使用 `MiniDumpWriteDump` 抓取 Triage 或 Full dump。
- `tools/convert-novaflare-frame-log.ps1`：把进程内 Haxe 帧计数日志转换为稳定 CSV。
- `tools/monitor-novaflare-frame-rate.ps1`：仅作可选高频外部监视；无法证明计数器有效时必须失败关闭，不能再输出伪造大数。

Haxe `CrashHandler` 还必须把报告写入本次运行目录，并同时打印到标准输出；Windows 原生 runtime 必须在设置了 `NOVAFLARE_NATIVE_DUMP_DIR` 时安装未处理异常过滤器。正式性能结论只接受上述证据链产生的有效样本。

## 28. 第二批主线程热路径优化

当前 CPU 栈中 `Runtime::typeOf` 位列第一，`RootScope` 构造/析构合计也占据大量样本。本批执行：

- 把 `typeOf`/`isInstanceOf` 的空值和无 relocation 快路内联到生成代码，只在 relocation、view transition 或 GC phase 下进入完整 barrier 慢路。
- 将 `RootScope` 的正常托管线程严格 LIFO push/pop 保持为强制内联短路径；native re-entry、容量扩展、root snapshot barrier、跨线程和非 LIFO 销毁移入独立慢函数。
- 不削弱 root、pin、mark 或 relocation 正确性，不用关闭功能换取帧数。

本批原生 runtime 和全部 39 项 hxcpp 测试已完成编译并通过。由于头文件改动会触发生成 C++ 单元重编译，将与 Haxe CrashHandler、原生 dump、诊断 telemetry 一并合并，只做一次必要的游戏构建。

## 29. Full → Young remembered-set overflow 正确性修复

在继续做帧率优化前，默认 ZGC 长测暴露了第二个必须先修复的正确性问题：Full 回收完成后，下一次 Young 的 remembered-set overflow 恢复路径可能继续读取已经失效的内部引用。它不是普通的性能抖动，而是会把后续性能样本变成无效崩溃样本。

本批修改：

- 在 `hxcpp/runtime/gc/runtime.cpp` 中修复 Full → Young overflow 恢复时的 stale interior reference；
- 在 `hxcpp/tests/gc_remembered_buffer.cpp` 中增加对应回归覆盖；
- 保留 remembered set、relocation、自愈和精确 slot 语义，没有通过关闭移动或关闭 Young 来绕开问题。

验证结果：

- 目标测试通过；
- Young、remembered buffer、relocation、concurrent relocation 相关 4/4 测试通过；
- 全套 hxcpp 39/39 测试通过；
- 游戏只重新编译受影响单元并重新链接，没有再次触发 2774 单元全量编译。

证据：

- `_build/overflow-stale-ref-target-test-20260723.log`
- `_build/overflow-stale-ref-related-tests-20260723.log`
- `_build/overflow-stale-ref-full-suite-test-20260723.log`
- `_build/overflow-stale-ref-game-incremental-build-20260723.log`
- 恢复点：`_build/restore-before-overflow-stale-ref-fix-20260723-2005`

## 30. “定期强制 Full”实验为何没有进入默认配置

曾使用 `HXCPP_NOVAGC_FULL_EVERY_YOUNG=4` 做 120 秒受控实验。该运行的 7 项采集契约完整、正常退出且没有崩溃，但 Full 会额外带来更高尾延迟，不能解决主线程屏障、根包装和 Young final pause 的结构性成本。

保留的实验目录：

- `_build/novagc-full-every-4-overflow-fix-smoke-120s-20260723`

结论：

- 强制 Full 可以改变堆年龄分布，但会把额外 Full 暂停引入正常主菜单路径；
- 它只是在暂停频率、单次成本和内存之间换挡，不是默认帧率优化；
- 默认配置继续使用自适应 Young，Full 只由真实策略条件或显式请求触发。

## 31. 58.212 ms 巨型 Young 的精确根因

### 31.1 时间线证据

修复前默认 180 秒样本：

- `_build/novagc-default-overflow-fix-clean-180s-20260723`

关键周期：

| 周期 | 时间关系 | used-before | marked / promoted | 总 STW |
|---|---:|---:|---:|---:|
| Young #6 | 前一正常周期 | 75.7 MiB | 13,127 / 3,817 | 7.411 ms |
| Young #7 | 与 #6 间隔 50.222 s | 334.1 MiB | 784,329 / 784,345 | 58.212 ms |

Young #7 的暂停拆分为：

- initial pause：20.661 ms；
- final pause：37.551 ms；
- 合计：58.212 ms。

它与 Haxe 约 54.9 秒处的 `update_worst_ms=65`、`draw_worst_ms=42` 同窗。也就是说，旧版本不是“每次 Young 都慢”，而是恢复自动 GC 后跳过了约 50 秒 Young，让一个异常大的新生代批次一次性进入暂停阶段。

### 31.2 代码根因

根因位于 `hxcpp/runtime/core/runtime_facade.cpp` 的 `__hxcpp_enable(true)`：

1. 自动 GC 被暂时抑制时，分配仍会累计；
2. 恢复时，如果抑制期间的债务没有达到 16 MiB，代码不会立即请求 Full；
3. 旧代码无条件把下一次自动 GC 阈值设为：

   `allocated + automaticGcMaximumHeadroom`

4. `automaticGcMaximumHeadroom` 默认是 256 MiB；
5. 因此恢复后的下一次 Young 被错误推迟了 256 MiB，而不是使用 Young 自适应 headroom；
6. 最终形成约 334 MiB used-before、78 万级 live/promoted 对象的巨型 Young。

这也是为什么简单调大/调小 Young 区无法稳定修复：错误发生在“恢复时采用了 Full headroom”，不是正常 Young 自适应公式本身。

## 32. 恢复阈值修复

### 32.1 runtime 修改

`hxcpp/runtime/core/runtime_facade.cpp` 新增并维护：

- `currentAutomaticYoungHeadroom` 原子值；
- 初始化时发布配置的 Young headroom；
- 每次自适应调整后发布最新 Young headroom；
- 异常恢复阈值读取当前自适应值；
- `__hxcpp_enable(true)` 在启用 Young 时使用当前 Young headroom；
- 只有禁用 Young 时才回退到 Full maximum headroom。

新的恢复逻辑为：

```text
resume headroom =
    Young enabled ? current adaptive Young headroom
                  : Full maximum headroom
```

恢复请求、阈值和 grace deadline 仍按既有发布顺序写入，避免 mutator 在部分状态下重新运行。

### 32.2 回归测试

`hxcpp/tests/gc_automatic.cpp` 新增 small-debt resume 用例：

- Young trigger/min/max：4 MiB；
- Full maximum headroom：64 MiB；
- 抑制期间分配量小于一个 nursery；
- 恢复后继续分配约 200,000 个节点；
- finalizable 对象必须在 4 秒内被回收。

旧逻辑会等待额外 64 MiB 而超时；新逻辑会按 4 MiB Young headroom 及时恢复回收。

当前再次执行全套回归：

- `hxcpp-gc-automatic` 通过；
- 全套 39/39 通过；
- 0 项失败；
- 实际测试时间 2.27 秒。

证据：

- `_build/resume-young-threshold-full-suite-test-20260723.log`
- 恢复点：`_build/restore-before-resume-young-threshold-fix-20260723-2125`

## 33. 增量构建结果

为了避免重复全量编译，本批先完成 runtime、测试和 Haxe 诊断字段，再合并做一次游戏构建：

- 总翻译单元：2774；
- 实际重新编译：4；
- 重新链接：1 次；
- 构建成功；
- 当前 EXE SHA-256：

  `21BC458DA436EAAF94633CC77F64CBB7A9084EB137B4D7F1CA26F7E311DBD753`

Haxe 帧日志同时新增绝对时间字段：

```text
wall_time_ms=<Unix epoch milliseconds>
```

`tools/convert-novaflare-frame-log.ps1` 已兼容新旧两种日志格式。这样 GC 的 `timestamp_us`、Haxe 帧窗口、ETW 相对时间和进程 UTC 样本可以精确对齐，不再依赖人工估算启动偏移。

证据：

- `_build/resume-young-threshold-game-incremental-build-20260723.log`

## 34. 修复后 180 秒完整验证

### 34.1 采集契约

验证目录：

- `_build/novagc-resume-young-threshold-fix-180s-20260723`

`diagnostic-manifest.json` 结果：

- schema：3；
- 采集契约：7/7；
- Haxe stdout/trace：有；
- Haxe 错误及异常栈管线：有，错误行 0；
- 原生线程栈：有，WPR ETW 完整；
- 堆和 GC 周期：39 行；
- 帧率：169 行；
- 进程 CPU/内存/响应：616 行；
- Windows crash/error events：0；
- Haxe crash：0；
- native dump：0；
- hang：0；
- 正常退出码：0；
- 未强制终止。

### 34.2 同口径结果

帧和进程数据均丢弃前 50 秒预热；GC “后 50 秒”以第一次 GC 时间加 50 秒为界。

| 指标 | 修复前 | 修复后 | 变化 |
|---|---:|---:|---:|
| Update 均值 | 235.715 TPS | 235.635 TPS | 基本持平，受 240 上限约束 |
| Draw 均值 | 387.854 FPS | 483.929 FPS | +24.77% |
| Update worst P95 | 23 ms | 21 ms | -8.70% |
| Draw worst P95 | 24 ms | 20 ms | -16.67% |
| Update worst P99 | 29 ms | 32 ms | +3 ms |
| Draw worst P99 | 37 ms | 30 ms | -7 ms |
| CPU 均值 | 18.361% | 19.007% | +0.646 个百分点 |
| Private Bytes 均值 | 693.648 MiB | 577.642 MiB | -116.006 MiB / -16.72% |
| Working Set 均值 | 573.659 MiB | 437.063 MiB | -136.596 MiB / -23.81% |
| 全部 Young 平均暂停 | 13.823 ms | 10.899 ms | -21.15% |
| 全部 Young 最大暂停 | 58.212 ms | 38.729 ms | -33.47% |
| 50 秒后 Young 平均暂停 | 10.904 ms | 7.820 ms | -28.28% |
| 50 秒后 Young P95 | 58.212 ms | 14.241 ms | 巨型 Young 已消失 |
| 50 秒后 Young 最大暂停 | 58.212 ms | 14.469 ms | -75.14% |
| 最大相邻 Young 间隔 | 50.222 s | 7.359 s | 恢复调度正常 |
| allocation stalls | 0 | 0 | 无退化 |
| emergency collections | 0 | 0 | 无退化 |

补充解释：

- CPU 均值略升，但 Draw 吞吐提高 24.77%；`Draw FPS / CPU%` 这个仅用于同场景的效率代理从 21.124 提升到 25.461，约 +20.53%；
- 修复后正常 Young 更频繁，这是预期行为：把 50 秒累积的一次巨型回收拆回 5～7 秒间隔的小批次；
- 修复后结束时 used-after 约 243.589 MiB、committed 约 276.254 MiB；修复前分别约 352.932 MiB、378.023 MiB；
- 没有通过牺牲 allocation-stall、emergency 或崩溃安全换取这些数字。

## 35. `208/206 ms` 原始尖峰的 ETW 归因

修复后样本仍保留了一行原始异常：

```text
Haxe time_ms=115308
update_worst_ms=208
draw_worst_ms=206
```

没有删除或过滤这行。精确对齐结果：

- 该 Haxe 统计窗内没有 GC；
- 最近的 Young #28 在窗口前约 112 ms 完成，总暂停 14.241 ms；
- Update 和 Draw 内部分段监控在异常行前的最大执行时间分别只有约 1 ms 和 16 ms；
- ETW 10 ms 时间线中，主线程最大无 CPU 样本间隔只有 5.283 ms；
- NVIDIA/驱动线程最大无 CPU 样本间隔只有 5.695 ms；
- 因此不是进程整体失去调度，也不是 STW。

符号化主线程包容栈显示，该约一秒统计窗由以下路径主导：

| 路径 | 包容样本 |
|---|---:|
| `OpenGLRenderer.__renderDisplayObject` | 2756 |
| `Context3DDisplayObjectContainer.renderDrawable` | 2420 |
| `Stage.__render` | 465 |
| GC collector 路径 | 0 |

结论：这是 OpenFL 显示树/Context3D 渲染遍历侧的长帧，不是 ZGC 暂停。它继续计入原始 `max`，但不能用来否定或夸大 GC 修复。

保留的文本证据：

- `cpu-stack-anomaly-timeline.txt`
- `cpu-stack-anomaly-segments-resolved.txt`
- `cpu-stack-anomaly-symbols.tsv`

## 36. 与 JDK Generational ZGC 的有效对比

对照对象必须是现代 Generational ZGC，而不是已经淘汰的 non-generational 模式：

- [JEP 439: Generational ZGC](https://openjdk.org/jeps/439) 在 JDK 21 引入分代 ZGC；
- [JEP 490: Remove the Non-Generational ZGC Mode](https://openjdk.org/jeps/490) 在 JDK 24 删除 non-generational 模式；
- 因此当前 `-XX:+UseZGC` 的架构方向是分代、并发、colored pointers、load/store barrier 和精确 remembered set。

JDK ZGC 与项目 NovaGC 的关键差异：

| 机制 | JDK Generational ZGC | 当前 hxcpp NovaGC |
|---|---|---|
| barrier fast path | JIT 直接注入，x64 可用紧凑 shift/check | AOT C++ 模板、包装器和 runtime 快/慢路径 |
| barrier phase 值 | 可做 barrier patching，减少全局/TLS 读取 | 仍有较多原子 phase/view 读取 |
| store slow path | act-once、medium path、线程本地 store buffer | 仍有较多直接 runtime/bitmap 工作 |
| remembered set | 精确、双缓冲 bitmap，GC 与 mutator 分离使用 | 精确 bitmap 已有，但 overflow/恢复和发布协议仍在完善 |
| roots | 栈/寄存器使用 colorless pointer，编译器深度协作 | 大量 `RootedValue`、`HX_NOVAGC_VARI`、`RootScope` 显式管理 |
| relocation | 主要并发，暂停目标通常低于 1 ms | Young final 仍承担 root/weak/remap/部分 relocation 工作 |
| 对象布局 | HotSpot 紧凑对象布局与成熟 region 元数据 | 当前固定 64 字节对象头仍有 cache/带宽成本 |
| 调度 | 成熟的分代 sizing、并发线程与暂停目标控制 | 自适应 Young 已有，但本轮才修复 suppression-resume headroom |

本轮修复与 JDK 的共同方向是：Young 必须以自适应小批次稳定触发，不能在恢复后错误积累一个超大 cohort。  
但本轮没有让 NovaGC 自动获得 JDK 的 JIT barrier patching、store buffer、colorless stack roots 或亚毫秒暂停能力，所以不能把“58 ms 降到 14 ms”描述成“已经等同 JDK ZGC”。

## 37. 当前结论与后续 ZGC 优先级

已经确认解决：

- 自动 GC 恢复后错误等待 256 MiB 的调度缺陷；
- 50.222 秒 Young 空窗；
- 58.212 ms 巨型 Young；
- 与之对齐的 65/42 ms GC 长帧；
- 大量额外 live/promoted 对象导致的堆膨胀；
- Full → Young overflow stale interior reference 正确性问题。

仍需继续优化：

1. 50 秒后 Young 最大 14.469 ms，仍高于 JDK ZGC 的典型亚毫秒目标；
2. `RefSlot::load`、load/store barrier 仍占主线程若干百分点；
3. 显式 root/pin 数量和固定 64 字节对象头仍造成吞吐、cache 与内存成本；
4. Young final pause 中仍有 root、weak/ephemeron、remap 和 relocation 工作；
5. OpenFL 显示树渲染存在独立的非 GC 长帧，需与 ZGC 指标分开处理。

下一批 ZGC 工作按风险和收益排序：

1. 对 store barrier 增加真正的线程本地缓冲/medium path，减少每次写入进入共享 runtime；
2. 把 barrier phase/view 状态进一步缓存或做 AOT 可补丁快路；
3. 分离并并发化 Young final 中可移动的 remap/relocation 工作；
4. 降低生成代码显式 root 包装数量，并保持 native re-entry 与异常安全；
5. 在对象 ABI 版本门禁完成后，再评估缩小 64 字节对象头；
6. 每批修改继续使用相同的 7/7 采集契约、39 项回归、一次必要增量构建和至少 180 秒长测。

用户已明确停止 Immix 方向；从本节开始不再构建、修复或调参 Immix，对照只保留历史原因分析和 JDK ZGC 架构参考。

## 38. 本阶段空间清理

在把 ETW 解析为可复查的符号文本后，已删除可再生的大型中间产物：

- 修复后 180 秒原始 ETL；
- ETLX 转换缓存；
- 临时未剥离符号 EXE；
- 临时 linker map 和 response file；
- 重复 CSV/未解析文本；
- 6 份已经被后续恢复点取代的 2026-07-19～20 大型 GCH/PCH 恢复目录。

本次实际释放：

- 15 个白名单目标；
- 5,355,621,688 bytes；
- 4.988 GiB；
- D 盘可用空间从 27.827 GiB 增加到 32.814 GiB。

继续保留：

- 当前和本日关键代码恢复点；
- 7/7 `diagnostic-manifest.json`；
- Haxe stdout、错误/异常栈管线结果；
- GC CSV/JSON、帧率 CSV、进程资源 CSV；
- 已符号化整段 CPU 栈摘要；
- `208/206 ms` 异常的 10 ms 时间线和分段符号栈；
- 39/39 测试日志与增量构建日志。

`_build/hxcpp-zgc-kernel` 没有删除，因为它是后续 39 项增量测试所需的对象缓存；删除它会违背“尽量使用增量编译”的要求。

## 39. 第二阶段：`RefSlot` 原子槽窄范围强制内联

### 39.1 修改与恢复点

恢复点：

- `_build/restore-before-refslot-force-inline-20260723-2205`

修改文件：

- `hxcpp/include/hx/gc/abi.hpp`

新增跨编译器 `HX_NOVAGC_ABI_ALWAYS_INLINE`，只应用到：

- `RefSlot::load`
- `RefSlot::store`
- `RefSlot::exchange`
- `RefSlot::compare_exchange_weak`
- `RefSlot::compare_exchange_strong`

没有改变原子对象类型、memory order、CAS 失败语义、barrier 逻辑、collector
算法和堆参数。没有继续把 always-inline 扩展到 `ObjectPtr::get`、
`Dynamic::Cast` 或完整 load/store barrier，避免代码膨胀和指令缓存风险。

### 39.2 测试结果

- automatic GC 目标构建通过；
- automatic GC 目标测试通过；
- 目标测试二进制 `RefSlot::load` defined symbol 为 `0`；
- 最终 hxcpp 全套测试 `39/39` 通过；
- 第一轮 `-j12` thread-shutdown 是 5 秒边界假超时；
- 单项复测通过，随后 `-j4` 全套复测通过。

证据：

- `_build/refslot-force-inline-target-build-20260723.log`
- `_build/refslot-force-inline-target-test-20260723.log`
- `_build/refslot-force-inline-full-suite-retest-20260723.log`

## 40. PCH 依赖遗漏：为什么第一次游戏构建是假有效

第一次游戏构建报告编译 `1561/2774` 个翻译单元并成功链接，但真实游戏对象
旁路符号链接仍然定义 `hx::gc::RefSlot::load`，且存在大量真实调用。

检查发现：

- `hxcpp/include/hxcpp.h.gch` 时间早于修改后的 `hx/gc/abi.hpp`；
- `HxcppZgcBuild.hx` 的 `pchInputs` 漏掉了 `hx/gc/abi.hpp`；
- 实际构建入口是预编译的 `hxcpp/run.n`；
- 只修改 `HxcppZgcBuild.hx` 而不重建 `run.n`，构建逻辑不会变化。

恢复点：

- `_build/restore-before-zgc-pch-abi-dependency-fix-20260723-2304`

修复：

1. 在 `hxcpp/tools/HxcppZgcBuild.hx` 的 `pchInputs` 中加入
   `hx/gc/abi.hpp`；
2. 用 NovaGC Haxe wrapper 重新生成 `hxcpp/run.n`；
3. 构建时确认打印 `rebuilding precompiled runtime header`；
4. 一次性执行公共 ABI 变化必需的 `2774/2774` 全量编译；
5. 通过精确 ABI 检查和正式链接；
6. 用真实游戏对象做不剥离符号的旁路链接。

有效构建结果：

- 日志：`_build/refslot-force-inline-pch-valid-game-build-20260723.log`
- 编译：`2774/2774`
- 构建成功：是
- 精确 ABI 检查：通过
- 正式 EXE：`export/release/windows/bin/NovaFlare Engine.exe`
- EXE 大小：`135102464` bytes
- EXE SHA-256：
  `FEE1AB607F726AB66D2464C3410FAD189D6E83D0A31ED335ADBB747DB4FFEC14`
- 真实游戏旁路链接 `RefSlot::load` defined symbol：`0`
- 新 PCH SHA-256：
  `9EFF279138E9368315D8E93E114D0B8EC008D713252739AE7F4EA53F811BCF9B`
- 新 `run.n` SHA-256：
  `A42E2ADA18F1B04FC5C3E2B96FBB8755F89A27F5526709682D6CA6CC47B7B3A3`

结论：公共 ABI 头变更后，“链接成功”不能替代 PCH 失效验证。今后必须同时
检查 PCH 输入依赖、重建提示、翻译单元数量和真实游戏符号。

## 41. 第一轮 180 秒为何不能作为 FPS A/B

第一轮有效新 EXE 诊断目录：

- `_build/novagc-refslot-force-inline-pch-valid-180s-20260724`

采集结果：

- `7/7` 契约完整；
- 47 条 GC；
- 167 条帧记录；
- 617 条进程记录；
- ETW 成功启动和停止；
- 退出码 `0`；
- 无 Haxe 异常、原生崩溃、dump、挂起和 Windows crash event。

最初把该轮与第一阶段 180 秒结果直接比较时，看到：

- update 均值下降约 `3.3%`；
- draw 均值下降约 `37.4%`；
- CPU 小幅上升；
- Private Bytes 与 Working Set 明显上升；
- 分配率和 pinned heap 同时上升。

继续检查 Haxe state 日志后发现对照错误：

- 旧基线 manifest 虽然写入 `scenario_input_sent=true`；
- 但旧基线 Haxe 日志 180 秒全程都是 `states.titleState.TitleState`；
- 新版本约 28.96 秒真实进入 `states.mainMenuState.MainMenuState`。

因此上述 FPS、分配率和内存差异主要混入了 Title 与 MainMenu 的场景差异，
不能用于决定保留或回退 `RefSlot` 修改。该轮只保留为 MainMenu 稳定性、
错误栈、堆和 ETW 证据。

诊断门禁追加一条：

> 场景是否进入必须以 Haxe 实际 state 日志为准，不能只相信按键发送成功或
> `scenario_input_sent`。

## 42. 公平的 Title→Title 180 秒复测

补做目录：

- `_build/novagc-refslot-force-inline-title-180s-20260724`

采集完整性：

- schema 3；
- `7/7` 采集契约完整；
- 180 秒测量窗口完成；
- 38 条 GC CSV/JSON；
- 169 条帧样本；
- 616 条进程样本；
- Haxe 日志与异常栈管线完整；
- ETW 原生线程栈完整；
- Windows crash/error events 为 `0`；
- Haxe crash、native dump、hang 均为 `0`；
- 退出码 `0`；
- 未强制终止；
- Haxe 日志全程为 `TitleState`。

统一口径：

- 帧与进程数据丢弃前 50 秒；
- GC 稳态从首次 GC 时间加 50 秒开始；
- 与第一阶段
  `_build/novagc-resume-young-threshold-fix-180s-20260723`
  做 Title→Title 比较。

| 指标 | 第一阶段 Title | `RefSlot` Title | 变化 |
|---|---:|---:|---:|
| Update 均值 | 235.635 | 236.079 | `+0.19%` |
| Draw 均值 | 483.929 | 438.016 | `-9.49%` |
| Update P95 | 21 ms | 23 ms | `+2 ms` |
| Update P99 | 32 ms | 33 ms | `+1 ms` |
| Update max | 208 ms | 34 ms | 极端尖峰未复现 |
| Draw P95 | 20 ms | 25 ms | `+5 ms` |
| Draw P99 | 30 ms | 33 ms | `+3 ms` |
| Draw max | 206 ms | 36 ms | 极端尖峰未复现 |
| CPU | 19.007% | 18.728% | `-0.279` 个百分点 |
| Private Bytes | 577.642 MiB | 560.887 MiB | `-16.755 MiB` |
| Working Set | 437.063 MiB | 445.273 MiB | `+8.210 MiB` |
| GC 次数 | 39 | 38 | `-1` |
| 分配速率 | 4.146 MiB/s | 3.925 MiB/s | `-5.33%` |
| 全部 GC 平均 | 10.899 ms | 10.497 ms | `-3.69%` |
| 全部 GC P95 | 34.096 ms | 35.429 ms | 启动期基本持平 |
| 全部 GC max | 38.729 ms | 44.701 ms | 启动期单点波动 |
| 稳态 GC 平均 | 7.820 ms | 6.805 ms | `-12.98%` |
| 稳态 GC P95 | 14.241 ms | 9.943 ms | `-30.18%` |
| 稳态 GC max | 14.469 ms | 10.329 ms | `-28.61%` |
| 稳态 used-after | 222.570 MiB | 211.549 MiB | `-11.021 MiB` |
| 稳态 committed | 254.052 MiB | 238.326 MiB | `-15.726 MiB` |
| 稳态 pinned | 143.472 MiB | 135.536 MiB | `-7.936 MiB` |
| 稳态 old | 78.170 MiB | 75.932 MiB | `-2.238 MiB` |

结论：

- update 吞吐持平；
- CPU、Private Bytes、分配率、稳态 heap 与稳态 GC 都有改善；
- Working Set 小幅上升，和 Private/committed 方向不同，暂不能归因于 GC；
- P95/P99 小尾部有波动，不能宣称帧稳定性全面解决；
- 两个 200 ms 级极端值没有复现，但不能仅凭一次运行认为永久消失。

## 43. 同场景 ETW：屏障收益与 draw 回退分离

Title→Title ETW 样本：

| 指标 | 第一阶段 Title | `RefSlot` Title |
|---|---:|---:|
| 总样本 | 284243 | 285590 |
| 主线程样本 | 149641 / 52.645% | 150841 / 52.817% |
| 渲染线程样本 | 126087 / 44.359% | 126180 / 44.182% |
| `RefSlot::load` 叶样本 | 2618 / 1.750% 主线程 | 0 |
| `Runtime::loadBarrier` 叶样本 | 545 / 0.364% | 562 / 0.373% |
| `Runtime::storeBarrier` 叶样本 | 2097 / 1.401% | 2295 / 1.521% |
| 三类直接屏障合计 | 5260 / 3.515% | 2857 / 1.894% |

直接屏障叶样本约下降 `46%`，真实游戏独立 `RefSlot::load` 热点消失。
主线程和渲染线程在总样本中的占比几乎不变，没有出现 ZGC 抢占更多 CPU 的证据。

Haxe 稳态渲染分段：

| 指标 | 第一阶段 Title | `RefSlot` Title |
|---|---:|---:|
| `FlxGame.draw.avg total_us` | 297.236 | 300.516 |
| `OpenGLRenderer.avg total_us` | 1039.803 | 1077.945 |
| `Stage.render.avg total_us` | 1101.134 | 1156.547 |
| `RenderThread exec_avg_ms` | 1.631 | 1.796 |
| `RenderThread avg_commands` | 257.036 | 251.240 |

Draw FPS 下降约 `9.5%` 与 OpenGL/RenderThread 执行时间上升一致；每帧命令数
没有增加，CPU 线程占比也没有结构性变化。当前证据更支持 GPU/驱动时序波动，
而不是 `RefSlot` 内联造成 ZGC 总开销回退。

决定：

- 保留 `RefSlot` 窄范围强制内联；
- 不扩大到上层包装器；
- draw 回退继续记录为独立图形路径问题；
- 下一轮仍需同场景复测，不能把一次波动当成永久结论。

保留的解析证据：

- `_build/novagc-refslot-force-inline-title-180s-20260724/cpu-stack-summary-resolved.txt`
- `_build/novagc-refslot-force-inline-title-180s-20260724/cpu-stack-symbols.tsv`
- `_build/novagc-refslot-force-inline-title-180s-20260724/haxe-stdout.log`
- `_build/novagc-refslot-force-inline-title-180s-20260724/gc-cycles.csv`
- `_build/novagc-refslot-force-inline-title-180s-20260724/frame-rate-from-haxe.csv`
- `_build/novagc-refslot-force-inline-title-180s-20260724/process-samples.csv`

## 44. 第二阶段空间清理

MainMenu ETW 解析完成后精确删除：

- 原始 ETL；
- ETLX；
- NGENPDB 临时目录。

释放：

- `4,104,264,923` bytes；
- `3.822 GiB`。

Title ETW 与真实符号验证完成后精确删除：

- 原始 ETL；
- ETLX；
- NGENPDB 临时目录；
- 旁路符号 EXE、map、rsp；
- 已被正式 `run.n` 取代的 `run.n.building`。

释放：

- `4,570,046,360` bytes；
- `4.256 GiB`。

第二阶段合计释放 `8.078 GiB`；加上前一阶段 `4.988 GiB`，本轮持续执行期间
累计精确清理约 `13.066 GiB`。D 盘最终检查可用空间为 `32.117 GiB`。

保留：

- 正式游戏 EXE；
- 有效 PCH 与增量构建缓存；
- 当前恢复点；
- 两轮 `diagnostic-manifest.json`；
- Haxe stdout 和异常栈管线结果；
- GC CSV/JSON、帧率与进程 CSV；
- 已符号化 ETW 摘要和符号表；
- 构建与测试日志；
- 两份白名单清理脚本，便于审计实际删除范围。

## 45. 第三阶段：CFFI byte-buffer 与 `Array<UInt8>::resize`

### 45.1 根因

第一版 CFFI byte-buffer 修改虽然消除了 `ensureCapacityPinned()` 的整段重复清零，
但暴露出另一条更昂贵的启动路径：

`Bytes.alloc → Array_obj<unsigned char>::resize → 逐元素 storeAtPinned`

在大字节数组扩容时，该路径让每一个 byte 都经过槽存储接口。20 秒诊断中，
Young #7 因而回退到 `72.983 ms`，说明只移动清零位置并不能解决问题。

### 45.2 最终实现

`array.hpp` 的 `resize()` 现在区分：

1. 非托管算术数组换成全新 NovaGC 存储：利用分配器零初始化，不重复遍历；
2. 原容量内增长：只连续清零新增区间；
3. 缩容：连续清零被移除区间，防止随后增长暴露旧内容；
4. 外部数组缩容：先解除外部存储绑定，保持 copy-on-write；
5. 托管引用数组：继续逐槽走屏障，不改变 GC 正确性。

同时：

- `cffi` 的 `alloc_buffer_len` 进入 `ManagedMutatorScope`；
- PCH 输入补入 `array.hpp`；
- 新增外部数组 COW、零尾部、缩容/重增长和旧数据不可见测试。

### 45.3 生效验证

- NovaGC 专项：`39/39`；
- 游戏构建：`2774/2774`，链接及 precise ABI 检查通过；
- 游戏实际 `Bytes` 对象文件包含新模板实例；
- 反汇编确认新存储扩容跳过清零循环，原存储范围使用单次连续清零；
- `resize()` 不再调用逐元素 `storeAtPinned()`；
- `Bytes.alloc()` 确实落到该实现。

## 46. 第三阶段监测结果

### 46.1 20 秒启动期

| 版本 | Young #7 | Young #8 | 合计 |
|---|---:|---:|---:|
| 第一版 byte-buffer | 72.983 ms | 3.309 ms | 76.292 ms |
| fast-clear | 9.939 ms | 6.822 ms | 16.761 ms |

启动关键两次 GC 合计下降约 `78.0%`，且没有
`slow_handshake`、Haxe/native 异常、崩溃、卡死或 Windows 应用错误。

### 46.2 180 秒稳态

| 指标 | `RefSlot` 基线 | 第一版 byte-buffer | fast-clear |
|---|---:|---:|---:|
| update 均值 | 236.079 | 237.706 | 237.794 |
| draw 均值 | 438.016 | 416.198 | 438.587 |
| update low | 100.897 | 101.968 | 102.460 |
| draw low | 108.056 | 122.754 | 121.619 |
| 最差 update/draw 计数 | 34 / 36 | 24 / 26 | 54 / 31 |
| CPU | 18.728% | 20.741% | 20.739% |
| Private Bytes | 560.887 MiB | 586.968 MiB | 589.549 MiB |
| Working Set | 445.273 MiB | 457.236 MiB | 468.887 MiB |
| 线程数 | 26.878 | 28.870 | 28.813 |
| 分配率 | 2.634 MiB/s | 2.547 MiB/s | 2.627 MiB/s |
| GC mean/P95/max | 6.805 / 9.943 / 10.329 ms | 8.126 / 14.694 / 14.694 ms | 7.955 / 11.405 / 13.986 ms |

本阶段恢复了第一版 byte-buffer 的 draw 回退并修复启动长尾，但相对 `RefSlot`
基线仍有约 `10.7%` CPU、`5.1%` Private Bytes 和更高 GC 尾延迟。
因此保留正确性和启动收益，不宣称稳态 FPS 目标已经完成。

### 46.3 最差帧归因

`update=54`、`draw=31` 的 Haxe 样本已精确对齐 ETW：

- 尖峰不与任何 GC 重叠；
- 上一次 GC 已结束约 7 秒；
- 下一次 GC 在尖峰后约 493 ms 开始；
- 主线程、NVIDIA OpenGL 驱动线程以及其他线程同时缺少采样。

这是整进程被调度出去或 GPU/驱动/系统等待，不是 ZGC pause。

### 46.4 稳态屏障占比

主线程叶采样中：

- `storeBarrier` 约 `1.486%`；
- `loadBarrier` 约 `0.372%`；
- `concurrentRelocationActive` 约 `0.276%`；
- 合计约 `2.13%`。

该比例在第一版 byte-buffer 和 `RefSlot` 基线中也接近 `2.1%`，说明：

1. 屏障是稳定存在、可以继续缩减的成本；
2. 它不能单独解释约 `10.7%` 的总 CPU 差距；
3. OpenFL/OpenGL/NVIDIA 驱动仍是稳态 CPU 和非 GC 尖峰的重要来源。

## 47. 当前完整状态与决策

| 维度 | 状态 | 决策 |
|---|---|---|
| `RefSlot` 窄范围内联 | 保留 | 有实测收益和完整回归 |
| CFFI/byte-buffer 协议 | 保留 | native mutator 协议更完整 |
| `Array<UInt8>` fast-clear | 保留 | 启动收益明确，正确性测试通过 |
| 稳态 CPU | 未达标 | 继续分离 ZGC 固定开销和渲染/驱动开销 |
| GC mean/P95/max | 未达标 | 继续拆 final pause 和调度长尾 |
| 帧数稳定性 | 未达标 | GC 尖峰与系统/GPU 尖峰分别治理 |
| 内存占用 | 未达标 | 跟踪 Private Bytes、工作集、线程和堆组成 |
| 分配率 | 已排除为主因 | 当前与基线基本相同 |
| 日志证据链 | 完整 | Haxe、异常栈、线程栈、堆/GC、帧、资源、ETW、Windows 事件均已抓取 |
| 构建策略 | 符合 | 目标增量测试后只做一次必要游戏构建 |
| 空间清理 | 进行中 | 已释放约 `4.647 GiB`，新一轮大型可再生产物待固化摘要后删除 |
| 恢复能力 | 有效 | 本阶段两个精确恢复点继续保留 |

当前下一项 ZGC 候选是拆分 `storeBarrier` 的短 fast path 与 noinline slow path。
现实现让常见快路径也承担大慢路径带来的寄存器保存、栈帧和 TLS 查询成本。
该候选必须先通过目标反汇编、专项测试和 `39/39`，确认快路径机器码确实缩短后，
才允许进行一次必要的游戏增量构建和 20/180 秒 A/B。

## 48. 第四阶段：`storeBarrier` fast/slow path 隔离

### 48.1 恢复点与单变量边界

修改前建立：

- `_build/restore-before-storebarrier-slow-split-20260724-040520`

只修改：

- `hxcpp/runtime/gc/runtime.cpp`

未修改公共 `runtime.h`，没有扩大 ABI/PCH 影响面。实现增加跨平台 noinline 宏，
并用内部模板 helper 接收 `Runtime::Impl*`，避免新增公共成员或符号。

### 48.2 实现与机器码门禁

原 `Runtime::storeBarrier()` 把快路径和大量冷逻辑放在同一函数中。游戏基线机器码：

- 函数大小：`0x8e0`，即 2,272 B；
- 保存 `r15/r14/r13/r12/rbp/rdi/rsi/rbx`；
- 入口预留 `0x78` B 栈空间。

修改后：

- `Runtime::storeBarrier()` 大小：`0xa0`，即 160 B；
- fast path 无 push、无栈分配；
- 慢路径通过尾跳转进入约 `0x800` B helper；
- 热偏移为 `+0x3c/+0x45/+0x54/+0x6f`；
- 游戏实际对象与旁路符号 EXE 均确认该机器码。

慢 helper 处理：

- young mark active；
- concurrent relocation；
- old-to-young remember；
- mark/SATB；
- relocation lock 等待时的 safepoint poll。

快路径原有判断顺序没有改变。

### 48.3 增量构建与回归

执行结果：

1. runtime 单元构建通过；
2. 8 个高风险 barrier 专项测试全部通过；
3. NovaGC 完整测试 `39/39` 通过，总耗时 27.82 秒；
4. 游戏构建只重新编译 `1/2774` 个翻译单元；
5. 唯一改变对象：
   `2753_runtime_cbf9643f869d68b73601.o`；
6. precise ABI 探针通过；
7. 正式 EXE PE、哈希、节地址和旁路符号匹配通过。

构建期间曾出现两个包装脚本短暂重叠并争用日志，但构建后完成了强校验：

- 没有残留构建进程；
- 时间窗口内只改变一个 runtime 对象；
- 对象哈希、ABI、ApplicationMain 和正式 EXE 均有效。

因此它是编排瑕疵，不是源代码或构建结果失败。

### 48.4 20 秒启动诊断

20 秒 Title 诊断：

- 7/7 采集完整；
- 退出码 0；
- 无 Haxe/native/Windows error、crash 或 hang；
- 无 `slow_handshake`、overflow 或 fallback。

Young #7/#8：

| 运行 | Young #7 | Young #8 | 合计 |
|---|---:|---:|---:|
| fast-clear 基线 | 9.939 ms | 6.822 ms | 16.761 ms |
| 本阶段 20 秒 | 13.407 ms | 2.784 ms | 16.191 ms |
| 本阶段干净 180 秒启动段 | 12.382 ms | 4.509 ms | 16.891 ms |

启动总量处于同一波动范围，没有回退到旧的 70 ms 级长尾。

### 48.5 第一轮 180 秒污染归因

第一轮目录：

- `_build/novagc-storebarrier-slow-split-title-180s-20260724`

运行本身完整且退出码 0，但采集期间执行了额外 PowerShell 读取/分析命令。异常簇：

- 约出现在游戏时间 65–116 秒；
- 最差记录达到 `1992/1943 ms`；
- 最差帧与相邻 GC 分别相隔约 4.351 秒和 7.923 秒；
- 进程监控自身出现约 3.3 秒采样间隙；
- 84–89 秒窗口 PowerShell PID 18904 有 3,564 个样本，游戏只有 1,795 个。

所以该轮不是 ZGC 长停顿，而是测试工具污染；数据不得进入正式 A/B。
保留了全部小型日志、CSV、解析文本和
`EVIDENCE_SUMMARY.md`，随后删除可再生 ETL/ETLX。

### 48.6 干净 180 秒正式结果

第二轮目录：

- `_build/novagc-storebarrier-slow-split-title-180s-clean-20260724`

采集期间不运行任何额外读盘、分析或性能查询命令，只轮询已启动的同一会话。

完整性：

- 7/7 采集目标；
- 退出码 0；
- 40 个 GC；
- 169 个 Haxe 帧；
- 621 个进程记录；
- Haxe 异常栈、原生 crash/hang dump、Windows 错误事件均为 0；
- 无 `slow_handshake`、overflow、fallback 或无响应记录。

50–180 秒：

| 指标 | fast-clear | 本阶段 | 变化 |
|---|---:|---:|---:|
| update | 237.794 | 237.325 | `-0.20%` |
| draw | 438.587 | 523.048 | `+19.26%` |
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

简单的 `CPU/draw` 辅助归一化值从 4.729 降到 4.002，约改善 15.37%。
该值不能替代硬件能耗计数，但说明 CPU 绝对值增加 0.192 个百分点与
draw 吞吐增加 19.26% 同时发生，不能判定为屏障回退。

### 48.7 ETW、最差帧和 slow helper

ETW 时间对齐：

- 目标进程窗口：276.641–180,038.810 ms；
- 游戏时间到 ETW 偏移约 2,128.963 ms；
- 主线程 TID 916；
- NVIDIA/渲染驱动线程 TID 19440。

50–180 秒：

- 游戏总样本 233,971；
- 主线程 108,193；
- 渲染/驱动线程 109,199；
- 四个同口径 `storeBarrier` 叶样本：
  `699+558+213+135=1,605`；
- 占主线程 `1.483%`，旧 fast-clear 约 `1.486%`；
- `storeBarrierSlow` inclusive 为 431，约 `0.398%`；
- 没有线程卡死在 slow helper。

直接叶占比没有显著下降。本阶段的确定性收益是 fast path 不再承担大函数序言、
8 个非易失寄存器保存和 `0x78` B 栈帧。

最差 update 42 ms 出现在游戏时间 140,646 ms：

- 前一 GC 早 4,348.077 ms，暂停 5.465 ms；
- 后一 GC 晚 1,088.179 ms，暂停 4.789 ms。

所以最差帧不是 ZGC pause。

系统分析：

- 50–180 秒 12,788 个 10 ms 桶；
- 全系统零样本桶为 0；
- 低样本桶为 13；
- 50–65 秒低样本桶为 0；
- 最差帧附近目标最长无样本仅 10 ms；
- 没有第一轮 PowerShell 抢占簇。

### 48.8 保留、清理与当前结论

决定保留 `storeBarrier` 冷慢路径拆分，不回滚。

保留理由：

1. 机器码目标完全实现；
2. 专项、`39/39`、ABI 和游戏实际生效全部通过；
3. 干净复测没有 CPU、内存、线程、帧率或 GC 的明确回退；
4. draw、low、最差帧和 GC mean/P95/max 均改善。

已清理：

- 受干扰运行 ETL/ETLX：约 3.570 GiB；
- 干净运行 ETL/ETLX；
- 20 秒运行 ETL；
- 旁路符号 EXE/map/rsp。

后五类合计释放 4.860 GiB，本阶段总计约 8.430 GiB。
D 盘可用空间约 25.444 GiB。

保留：

- 正式游戏 EXE；
- 增量构建缓存；
- 精确恢复点；
- 构建和测试日志；
- diagnostic manifest；
- Haxe、异常栈、GC、帧率、进程与 Windows 事件日志；
- CSV/JSON；
- 已解析堆栈、符号表和系统分析文本。

详细摘要：

- `_build/novagc-storebarrier-slow-split-title-180s-clean-20260724/EVIDENCE_SUMMARY.md`

当前结论不是“ZGC 已经全部追上 Immix”，而是：

- 本轮 ZGC 固定路径优化有效，应保留；
- Title 场景的帧数、low 和 GC 尾延迟均改善；
- CPU/内存未出现相对 fast-clear 的明确回退；
- `storeBarrierSlow` 仍有约 0.398% 主线程 inclusive 成本；
- 下一阶段必须转入玩法负载并轮换排查其他维度。

## 49. 第五阶段：Gameplay 周期 Full 调度优化

### 49.1 阶段目标

本阶段不再修改 Immix，也不处理 Freeplay/BPM。目标限定为：

1. 建立可重复的真实 Gameplay 入口；
2. 抓取 Haxe 日志、异常栈、原生线程栈、堆、GC、帧、CPU、内存和
   Windows 错误；
3. 找出 ZGC 周期性大长帧与平均吞吐的真实原因；
4. 与本地 JDK 26 Generational ZGC 的调度策略做源码级对照；
5. 只实施一个可回退、可增量构建的 Major 调度变量；
6. 正式 EXE 至少完成 225 秒全量监测。

### 49.2 Gameplay 诊断入口

`InitState` 增加只由诊断环境变量触发的 Gameplay 入口：

- song：`epiphany`；
- chart：`epiphany-hard`；
- difficulty：2；
- mod：`Doki Doki Takeover Plus`；
- botplay：true。

入口先准备非空静音 music 对象，再直接创建 `PlayState`，避免
`FlxG.sound.music` 为空导致启动路径失败。

runner 增加：

- `Scenario=Gameplay`；
- 歌曲、难度、mod、botplay 参数；
- `diagnostic:gameplay prepared` 验证；
- `perf:PlayState.create end` 验证；
- 30 秒无响应 Full dump；
- 225 秒测量窗口。

恢复点：

`_build/restore-before-gameplay-diagnostic-hook-20260724-045929`

### 49.3 最初 Gameplay 基线

正式基线目录：

`_build/novagc-gameplay-epiphany-clean-225s-20260724`

结果：

- 7/7 采集完成；
- 退出码 0；
- Haxe/native/Windows 错误为 0；
- crash/hang dump 为 0；
- GC 143 行；
- frame 191；
- process 771。

`PlayState.create` 总耗时 3,190 ms。启动期 cycle 8 Full 和 cycle 9
Young 分别形成约 334.619/362.718 ms 的大 cohort。

稳定 50–215 秒：

| 指标 | 值 |
|---|---:|
| update mean / P5 / low | 138.969 / 107.700 / 62.132 |
| draw mean / P5 / low | 139.686 / 112.200 / 61.509 |
| update worst max | 250 ms |
| draw worst max | 240 ms |
| Young+Full | 121+1 |
| pause mean / P95 / max | 7.778 / 10.394 / 12.142 ms |
| concurrent max | 230.738 ms |
| allocation rate | 约 11.77 MiB/s |
| CPU mean | 15.469% |

没有 allocation stall、timeout、emergency、fallback 或稳定窗口无响应。

### 49.4 固定 64-cycle 根因

Full 严格落在 cycle 8、72、136：

| cycle | 应用时间 | concurrent | pause | used before→after | 对应最大帧 |
|---:|---:|---:|---:|---:|---:|
| 8 | 34.669 s | 292.176 ms | 334.619 ms | 859.72→626.14 MiB | 启动 cohort |
| 72 | 128.879 s | 230.738 ms | 10.597 ms | 421.46→407.33 MiB | 250/240 ms |
| 136 | 217.219 s | 248.689 ms | 7.635 ms | 427.12→404.63 MiB | 177/146 ms |

周期 Full 只回收约 14–22 MiB，却让两个 GC worker 与主/渲染线程并行
竞争约 220–249 ms。

ETW cycle 72 窗口：

- 主线程并未停 250 ms，最长无采样约 9.068 ms；
- 渲染线程最长无采样约 166.494 ms；
- 两个 GC worker 同时活跃约 220 ms；
- 结论是并发吞吐干扰，不是单次 250 ms STW。

源码：

`hxcpp/runtime/core/runtime_facade.cpp`

```cpp
const bool fullDue = softMaxDue ||
    promotedDebt >= automaticFullPromotionBytes ||
    youngCyclesSinceFull >= automaticFullYoungCycles;
```

原默认 `automaticFullYoungCycles=64`。本轮 cycle 72/136 由第三个条件触发，
不是 soft max 或 promotion debt。

### 49.5 Gameplay 固定税

基线主线程稳定窗口的 ETW 直接叶近似：

| 路径 | 主线程占比 |
|---|---:|
| `loadBarrier` | 约 4.28% |
| `storeBarrier` | 约 1.89% |
| `ScopedPin` 构造+析构 | 约 3.96% |
| `concurrentRelocationActive` | 约 0.95% |
| GC 管理路径合计 | 约 12.08% |

Title 场景显著更低。说明真实玩法中的 Dynamic、数组、Shader 参数和显式
pin 会放大 NovaGC 的 AOT 固定税。这一问题独立于 64-cycle Full，尚未
在本阶段解决。

### 49.6 与 OpenJDK 26 Generational ZGC 对照

本地 JDK 26 `zDirector.cpp` 不使用“每固定 N 次 Young 必做 Major”：

- Minor 依据 soft/hard capacity、allocation rate 预测、波动、预计 GC
  时长和 OOM deadline；
- worker 数依据 deadline 和串/并行耗时动态选择；
- Major warmup 只在 soft max 10%/20%/30% 使用量建立样本；
- proactive Major 至少要求 Old 增长或 5 分钟时间条件，并检查吞吐损失；
- Minor 到期时，仅在 Old 回收收益足以摊销未来成本或内存紧急时升级
  Major。

NovaGC 固定计数不看回收量、Old 增长、headroom、上轮成本收益或吞吐
预算，是本次低收益 Full 尖峰的结构原因。

### 49.7 环境变量 128 两轮 A/B

同一旧 EXE，只设置：

`HXCPP_NOVAGC_FULL_YOUNG_CYCLES=128`

两轮 225 秒结果都只有启动 cycle 8 Full，运行结束前没有 cycle 72
Full；对应中段/尾部最大帧稳定在约 31–34 ms。

第一轮 128 相位对齐窗口：

- update/draw mean：127.637/126.681；
- update/draw max：31/34 ms；
- concurrent max：4.347 ms；
- stall/fallback/emergency：0。

第二轮 128：

- update/draw mean：125.059/124.778；
- update/draw max：31/30 ms；
- concurrent max：3.687 ms；
- stall/fallback/emergency：0。

两轮 128 的平均 FPS 低于最初 64 基线，但后续默认 64 顺序回测出现
122–175 update 和 124–243 draw 的巨大跨运行方差，因此不能从不相邻
运行推导 128 的平均吞吐回退。

### 49.8 扩展 CPU/GPU/窗口监控

新增：

- `tools/monitor-novaflare-hardware.ps1`；
- process CSV 的 foreground/visible/iconic/cloaked/窗口矩形；
- CPU `Processor Performance`、Utility；
- 目标 PID GPU Engine、Dedicated/Shared memory；
- runner manifest 的第八个采集目标；
- `tools/analyze-novagc-ab.ps1` 的硬件/窗口窗口统计；
- `tools/summarize-etw-main-leaves.ps1`。

恢复点：

`_build/restore-before-window-frequency-monitor-20260724-061350`

20 秒烟雾测试确认窗口、CPU 和 GPU 计数器有效；正式默认 64 硬件轮
213 个硬件样本，process/hardware stderr 都为 0。

cycle 72 前后：

- CPU Performance 约 120.4%→121.2%；
- GPU 3D 约 19.2%→18.1%；
- 前台、可见、未最小化、未 cloaked；
- FPS 没有持续上升。

因此一次默认 64 的异常高 FPS 跃迁不是可重复的 Full 提频收益。

### 49.9 默认值修改

恢复点：

`_build/restore-before-full-cycle-default128-20260724-062636`

修改：

```cpp
std::uint64_t automaticFullYoungCycles = 128;
```

以及：

```cpp
"HXCPP_NOVAGC_FULL_YOUNG_CYCLES", 128, 1, 1024
```

回归与构建：

1. NovaGC kernel 只编译 `runtime_facade.cpp`；
2. `39/39` 通过，总测试 29.66 秒；
3. 游戏只重新编译 `1/2774`；
4. precise ABI 通过；
5. runtime 对象 SHA-256：
   `C49F896D9708B928C3B0DDF9AC7A1696D167CFFA3D85ABF07731CD8BA8E8C746`；
6. 正式 EXE SHA-256：
   `B39E444B42375F61F614CBF1B8744EEA156BD234531634B4F5090767AD481414`。

Lime 输出 `Failed to update resources`，但 `obj/ApplicationMain.exe` 与
正式 bin EXE 长度、时间戳、SHA-256 一致，未残留旧 PE。

### 49.10 新默认 128 正式结果

目录：

`_build/novagc-gameplay-epiphany-default128-source-225s-20260724`

8/8 完整，退出码 0，所有 Haxe/native/Windows 错误为 0。

没有设置 Full 周期环境变量，最终运行到 cycle 124，只存在启动 cycle 8
Full，证明源码默认值已进入正式 EXE。

与紧邻的默认 64 硬件轮相位对齐：

| 指标 | 默认 64 | 新默认 128 |
|---|---:|---:|
| Full | 1 | 0 |
| update mean | 122.785 | 125.853 |
| draw mean | 124.052 | 125.309 |
| update P99/max | 37.600/169 ms | 29.000/33 ms |
| draw P99/max | 37.940/169 ms | 30.650/32 ms |
| concurrent max | 277.154 ms | 6.036 ms |
| allocation rate | 10.585 MiB/s | 10.699 MiB/s |
| CPU mean | 14.943% | 15.382% |
| Working Set mean | 1,017.362 MiB | 908.549 MiB |
| GC heap mean | 422.678 MiB | 405.125 MiB |

CPU Performance 没有上升，窗口状态一致。最终结论是默认 128 应保留。

### 49.11 空间状态

五个 A/B/回测目录中的 ETL、ETLX、NGENPDB 共 13,943,608,638 字节。
它们都已完成证据提取，可再生成。

两次 PowerShell 精确路径删除均在进程创建前被执行策略拦截，没有文件
被部分删除。当前保留是执行策略阻塞，不是技术分析需要。策略允许后应
优先删除这些大型产物，保留 manifest、CSV、日志、摘要、恢复点和增量
构建缓存。

## 50. 第六阶段：`PinnedBytes` 快路径、hang 误报和 128 反证

### 50.1 固定税候选选择

Gameplay 128 稳定主线程中：

- `ScopedPin` 构造+析构约 3.237%；
- `loadBarrier` 约 1.958%；
- `concurrentRelocationActive` 约 0.935%。

调用边确认 `PinnedBytes` 在每次 byte-array 浮点读写中创建两个
`ScopedPin`。managed scalar backing buffer 从分配起就是 `TypePinned`，
标量 memcpy 内又没有 safepoint，因此非 concurrent relocation 时重复 pin
是局部固定税。

历史恢复点证明全局强制内联 `ScopedPin` 曾被撤销，不能重复走代码膨胀
路径。本轮只改 `memory.cpp` 单一运行时单元。

### 50.2 修改与恢复点

恢复点：

`_build/restore-before-pinnedbytes-fastpath-20260724-073500`

修改：

1. 数组 shell 只在 concurrent relocation 活跃时 pin；
2. managed scalar buffer 使用固有 `TypePinned`；
3. unmanaged view 正确解释为 `ExternalArrayBuffer`；
4. unmanaged descriptor 只在并发搬迁期 pin；
5. buffer 引用使用 `loadBarrierFast`；
6. 增加 managed double 与 unmanaged float 回归。

门禁：

- `hxcpp-array` 1/1；
- 完整测试 39/39；
- 游戏 1/2774 增量；
- precise ABI 通过；
- 正式 EXE SHA-256：
  `701D71354304A4E251C78BD96C8E4FA85E5A930A53A85D9F70CFD054A32F20D7`。

### 50.3 首轮 hang dump 归因

首轮 225 秒被标记 `hung`，但 dump 精确符号化后主线程正在：

`JsonParser.parseString/parseRec` →
`Song.parseJSON/getChart/loadFromJson` →
`InitState.startDiagnosticGameplay`

exception thread/code 均为 0，自动 GC 线程正在等待，游戏后续正常进入
PlayState。根因是 runner 在进程创建后立即使用 10 秒阈值，把正常同步
启动误判为挂起。

runner schema 4 现在：

- Gameplay 启动期阈值 45 秒；
- `perf:PlayState.create end` 后运行期阈值 10 秒；
- arm 时重置无响应计时；
- manifest 记录阈值、arm 时间和检测阶段。

60 秒烟雾在 31.612 秒 arm，正式轮在 34.387 秒 arm；两轮均无误报。

### 50.4 两轮 ETW

三轮同长度稳定主线程窗口：

| 路径 | 旧 128 | 候选首轮 | 候选干净轮 |
|---|---:|---:|---:|
| 主线程样本 | 150,595 | 150,810 | 148,931 |
| `storeBarrier` | 2.139% | 2.098% | 1.763% |
| `loadBarrier` | 1.958% | 1.920% | 2.272% |
| `ScopedPin` ctor+dtor | 3.237% | 2.825% | 3.473% |
| relocation check | 0.935% | 0.967% | 1.049% |
| 近似 GC 直接叶 | 9.144% | 8.763% | 9.418% |

目标局部边：

| 边 | 旧 128 | 候选首轮 | 候选干净轮 |
|---|---:|---:|---:|
| `ScopedPin ctor ← PinnedBytes` | 112 | 0 | 0 |
| `loadBarrier ← PinnedBytes` | 83 | 0 | 0 |
| relocation check ← `PinnedBytes` | 0 | 0 | 36 |

局部目标在两轮候选都生效，全局 pin 比例却随更高数组/GPU 工作量变化。
因此保留修改，但不宣称已经解决所有 Gameplay 固定税。

### 50.5 干净 225 秒

目录：

`_build/novagc-gameplay-pinnedbytes-fastpath-clean-225s-20260724`

8/8、exit 0、错误/崩溃/hang/dump 全 0。相位对齐窗口：

| 指标 | 源码 128 | 干净候选 |
|---|---:|---:|
| update mean | 125.853 | 144.696 |
| draw mean | 125.309 | 143.585 |
| update P99/max | 29.000/33 ms | 34.640/39 ms |
| draw P99/max | 30.650/32 ms | 35.000/37 ms |
| pause mean | 9.232 ms | 8.477 ms |
| pause P95/max | 11.596/14.455 ms | 11.910/13.647 ms |
| allocation | 10.699 MiB/s | 11.981 MiB/s |
| CPU | 15.382% | 15.313% |
| CPU Performance | 119.818% | 122.188% |
| GPU 3D | 10.051% | 37.609% |

平均吞吐提高但 GPU 3D 约 3.7 倍，P99 小幅回退，不能独占归因。

### 50.6 固定 128 结论降级

干净轮到 cycle 147，在 209.922 秒出现 cycle 136 Full：

- pause 9.461 ms；
- concurrent 188.098 ms；
- used 448.912→440.969 MiB，仅回收约 7.943 MiB；
- 对应 update/draw 189/151 ms 长帧；
- stall/timeout/emergency/fallback 全 0。

这证明 128 只是在低吞吐轮把周期 Full 推出窗口。固定计数仍会在高
Young 吞吐下产生低收益并发竞争，不能作为最终调度器。

### 50.7 空间状态

当前 ETL/ETLX、首轮误报 dump 和旁路符号产物均已完成证据提取。精确删除
再次在执行前被策略拒绝，没有部分删除。当前 D 盘只剩约 3.475 GiB；
在空间恢复前不得发起新的大型 ETW 或全量构建。

完整阶段摘要：

`_build/novagc-gameplay-pinnedbytes-fastpath-clean-225s-20260724/EVIDENCE_SUMMARY.md`

## 51. 第七阶段：SoftMax 压力门禁与 Major 并发竞争

### 51.1 阶段目标

本阶段不再修改 Immix，也不处理 Freeplay/BPM。唯一目标是把固定周期和
SoftMax 触发的低收益 Full 从 Gameplay 稳态路径中移走，同时保留显式 Full、
promotion debt 和内存紧急情况下的正确性兜底。

本阶段恢复点：

- 第一版 Major 增长门禁前：
  `_build/restore-before-adaptive-major-gate-20260726-123422`
- SoftMax 门禁修复前：
  `_build/restore-before-softmax-major-gate-20260726-133348`

### 51.2 第一版 Major 增长门禁

`hxcpp/runtime/core/runtime_facade.cpp` 增加：

- 默认最小 Major 增长：64 MiB；
- 环境变量：`HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB`；
- Major heap 定义：Old + Pinned + old Large；
- `major_growth_since_full_bytes`；
- `major_growth_gate_passed`；
- `major_trigger_reason`；
- cadence 只有在增长门禁通过时才允许 Full；
- 显式请求、promotion debt 和当时的 soft-max 路径暂时绕过。

`Full` 的 reclaimed bytes 同时修正为真正的 Full 前后堆差，避免把上一次
Young 的局部统计误写成 Major 收益。

新增 `gc_telemetry` 回归验证：

1. cadence 到达但 Major 增长不足时不得 Full；
2. trigger reason 必须是 `cycle_growth_wait`；
3. 显式 Full 必须绕过增长门禁；
4. JSON/CSV 字段一致。

### 51.3 第一版 225 秒结果

目录：

`_build/novagc-gameplay-adaptive-major64-clean-225s-20260726`

这轮 109 个 cycle 内只有启动显式 Full，因为正式窗口没有到达 cadence 128。
它证明新代码已进入正式 EXE，但不足以验证 cadence 门禁。精确帧—ETW 对齐
还识别出两个不同性质的尖峰：

- update 1342 ms 的窗口内 CPU 样本极少，主线程最大离核约 536 ms，没有
  Full，也没有 GC 热路径，不能归因于 ZGC；
- 随后的 draw 963 ms 窗口 CPU 样本密集，主线程落在 OpenFL/OpenGL render，
  是渲染负载，不是 GC 暂停。

这一步阻止了把所有帧空洞继续错误归到 collector。

### 51.4 330 秒压力轮暴露 SoftMax 风暴

目录：

`_build/novagc-gameplay-adaptive-major64-long-330s-20260726`

这轮达到 190 个 GC cycle，并暴露旧默认
`HXCPP_NOVAGC_SOFT_MAX_HEAP_MB=512` 的硬触发缺陷：

| cycle | reason | reclaimed | pause | concurrent |
|---:|---|---:|---:|---:|
| 42 | `soft_max` | 28.00 MiB | 11.763 ms | 300.603 ms |
| 57 | `soft_max` | 30.61 MiB | 44.638 ms | 289.202 ms |
| 73 | `soft_max` | 22.24 MiB | 7.532 ms | 217.708 ms |
| 86 | `soft_max` | 18.58 MiB | 9.094 ms | 247.306 ms |
| 96 | `soft_max` | 13.37 MiB | 10.287 ms | 251.043 ms |
| 105 | `soft_max` | 17.16 MiB | 122.949 ms | 368.548 ms |
| 114 | `soft_max` | 6.01 MiB | 9.697 ms | 224.881 ms |
| 118 | `soft_max` | 4.48 MiB | 216.869 ms | 246.978 ms |
| 119 | `soft_max` | 0.49 MiB | 34.989 ms | 264.182 ms |
| 150 | `soft_max` | 14.37 MiB | 45.709 ms | 245.687 ms |

加上启动显式 Full 后共 11 次 Full，pause 合计 905.427 ms，concurrent
合计 2996.744 ms。根因是 Full 后堆只略低于 512 MiB，下一次 Young 很快
越线，触发下一次 Full；soft-max 绕过了第一版增长门禁。

这一轮的 `hung` 标记不是 GC 死锁。full dump 和 Haxe 日志证明 ResultsScreen
同步构建图表：

- `ResultsScreen.new graph_ms=41071`
- `percent_ms=41130`
- `end elapsed_ms=41147`

导致约 43 秒无帧。该时段没有 Full，必须作为独立应用层问题处理。

### 51.5 JDK 26 源码对照与第二版门禁

本地对照：

- `toolchains/openjdk-26-source/src/hotspot/share/gc/z/zArguments.cpp`
- `toolchains/openjdk-26-source/src/hotspot/share/gc/z/zDirector.cpp`

JDK 默认 SoftMax 为 MaxHeap 的 90%，`ZDirector` 综合 SoftMax 容量、分配率、
代际回收成本、高使用率、紧急度、warmup 和 proactive 条件选择 GC。它不把
每次 SoftMax 越线直接等价为一次 Major。

第二版修改：

1. NovaGC 自动 SoftMax 默认值从 512 MiB 提高到 768 MiB；
2. `softMaxDue = exceeded && rateReady && growthGatePassed`；
3. cadence Full 同样受增长门禁约束；
4. 显式 Full 和 promotion debt 保留绕过能力；
5. reason 顺序：
   - `promotion_debt`
   - `soft_max`
   - `young_cycle_growth`
   - `soft_max_rate_wait`
   - `soft_max_growth_wait`
   - `cycle_growth_wait`
6. 新增遥测：
   - `major_soft_max_bytes`
   - `major_soft_max_exceeded`
   - `major_soft_max_rate_ready`
   - `major_soft_max_due`

新增 `hxcpp/tests/gc_softmax.cpp`，用极低 SoftMax 强制越线，同时用 64 MiB
增长门禁证明它停在 `soft_max_growth_wait` 而不会错误 Full；并验证 CSV
表头与数据列数。

### 51.6 回归和增量构建

执行结果：

- automatic/telemetry/softmax 目标测试：通过；
- NovaGC kernel：40/40；
- 游戏总翻译单元：2774；
- 实际重新编译：仅 `runtime_facade.cpp`，1/2774；
- precise ABI：通过；
- 没有触发全量游戏编译；
- 正式 EXE：
  `export/release/windows/bin/NovaFlare Engine.exe`
- SHA-256：
  `264D8AB43596BE8176C149EA0A7DC4F5A6CE57B570071EDB440E173941C91647`。

### 51.7 修复后 230 秒正式验证

目录：

`_build/novagc-gameplay-softmax-gated768-clean-230s-20260726`

完整性：

- capture contract：8/8；
- GC 154、frame 196、process 785、hardware 235；
- Haxe/native/Windows 错误：0；
- stall/timeout/emergency/fallback：0；
- native dump：0；
- SoftMax exceeded/due/wait：0；
- 测量窗口完整结束。

154 个 GC 中有 152 Young、2 Full。除了启动显式 Full，只有 cycle 136：

| 字段 | 值 |
|---|---:|
| reason | `young_cycle_growth` |
| Young since Full | 128 |
| Major growth | 69.55 MiB |
| used before | 511.195 MiB |
| used after | 470.816 MiB |
| reclaimed | 40.38 MiB |
| pause | 12.027 ms |
| concurrent | 207.915 ms |
| workers | 2 |

这说明 SoftMax 风暴已经消失，但 64 MiB cadence 门禁仍允许一次会与 Gameplay
竞争 CPU 的 Major。

### 51.8 A/B 结论和归因边界

旧 512 MiB 硬触发轮与新 768 MiB 门禁轮在共同 50～210 秒窗口：

- Full：9→1，下降 88.9%；
- pause 合计：2357.906→1113.570 ms，下降 52.8%；
- concurrent mean：22.440→3.312 ms，下降 85.2%；
- update mean：95.793→144.587 FPS；
- update P5：21.8→111.2 FPS；
- update worst P95：242.2→25.0 ms；
- GC heap mean：504.968→488.970 MiB；
- private bytes mean：1300.232→1302.039 MiB。

GC 次数和 GC 时间是强直接证据。FPS 只作为候选改善，因为候选 CPU
Performance 为 122.490%，基线为 111.038%，且候选 GPU counter 全 0
不可用。不得把全部 FPS 增幅归因于这一处调度修改。

A/B 数据：

`_build/novagc-gameplay-softmax-gated768-clean-230s-20260726/ab-softmax512-vs-gated768.json`

### 51.9 cycle 136 精确线程栈

ETW 精确窗口为 210.400～210.750 秒，共 582 个 CPU 样本：

- 主线程 TID 17580：276；
- 主线程最大离核间隙：12.135 ms；
- GC worker TID 18008/264：91/79；
- concurrent GC TID 4676/11216：44/36；
- 主线程持续在 Flixel update 和 OpenFL/OpenGL render；
- GC 线程落在 `startConcurrentMark` 和
  `beginConcurrentRelocation`。

结论：207.915 ms 是真实并发工作，不是同长度 STW。邻近 update/draw
200/162 ms 的主要 GC 机制是两个 Major worker 与满载主线程争夺 CPU/缓存。
初始/最终暂停总计只有 12.027 ms。

证据：

- `cpu-cycle136-exact-210.400-210.750-summary.txt`
- `cpu-cycle136-exact-210.400-210.750-hotspots.tsv`
- `cpu-cycle136-exact-210.400-210.750-hotspots-resolved.txt`

### 51.10 本阶段决策

保留第二版 SoftMax 门禁。理由：

1. 目标测试和 40/40 正确性回归通过；
2. 只增量编译 1/2774；
3. SoftMax Full 从 10 次降为 0；
4. 无堆安全回退；
5. 剩余 cycle 136 的机制已由 ETW 分离为并发 CPU 竞争，可继续单变量优化。

不宣称 ZGC 性能任务完成。下一实验必须先比较 Major worker 2→1 的同场景
效果，再决定做 worker 自适应，还是提高 cadence Major 的增长/收益门槛。

## 52. 第八阶段：Major worker 筛选与 128 MiB 收益门禁

### 52.1 全局 worker 2→1 为什么被否决

无重编筛选目录：

`_build/novagc-gameplay-softmax-gated768-workers1-clean-230s-20260726`

设置：

- `HXCPP_NOVAGC_MARK_WORKERS=1`
- `HXCPP_NOVAGC_RELOCATION_WORKERS=1`

当前配置入口同时影响 Young 和 Major，因此它不是纯 Major 实验。正式结果：

- 8/8、exit 0、错误/崩溃/hang/dump 为 0；
- GC 127：126 Young、1 启动显式 Full；
- 只到 cycle 127，没有覆盖原目标 cycle 136 cadence Major；
- update/draw mean 相比 2-worker 基线约下降 4%；
- P99 从约 31 ms 恶化到约 40 ms；
- 最大 106 ms 帧邻近 Young 只有 9.063 ms pause、0.865 ms concurrent；
- 精确 ETW 显示约 100 ms 全进程调度稀疏，不是 GC；
- CPU Performance 低约 2.7%，存在硬件方差。

决策：不设置全局 worker=1。以后如调整 worker，必须增加 Major 专用动态
选择，不能顺带改变 Young。

### 52.2 为什么把 Major 增长门禁改成 128 MiB

64 MiB 门禁正式轮 cycle 136：

- Young since Full：128；
- Major growth：69.55 MiB；
- used：511.195→470.816 MiB；
- reclaimed：40.38 MiB；
- pause：12.027 ms；
- concurrent：207.915 ms；
- workers：2；
- update/draw max：200/162 ms。

ETW 已证明 207.915 ms 不是 STW，而是两个 Major worker 与主线程同时运行
造成的竞争。SoftMax 为 768 MiB，当时堆和 Major growth 都没有内存紧急性。
Young 又能以约 1～3 ms concurrent 回收相近批次。因此 69.55 MiB 不足以
支付该 Major 成本。

修改：

```text
HXCPP_NOVAGC_FULL_MIN_MAJOR_GROWTH_MB default: 64 -> 128
```

仍可绕过门禁：

- explicit request；
- promotion debt。

仍作为独立压力输入：

- SoftMax 768 MiB；
- SoftMax exceeded；
- allocation-rate readiness。

恢复点：

`_build/restore-before-major-growth128-20260726-143500`

### 52.3 测试、编译器环境与构建

目标测试：

- automatic：通过；
- telemetry：通过，验证 `major_min_growth_bytes=134217728`；
- softmax：通过，继续验证显式 64 MiB 环境覆盖和
  `soft_max_growth_wait`。

首次 C++ 编译同时在两个单元静默 exit 1，没有诊断。直接运行
`cc1plus.exe --version` 得到 `0xC0000135`，确认本次 shell PATH 缺少
`C:\msys64\mingw64\bin`，导致编译器子进程找不到
`libgcc_s_seh-1.dll`、`libgmp-10.dll`、`libisl-23.dll` 等运行时 DLL。
恢复 PATH 后同一源码立即编译通过，不修改代码绕过。

全套：

- 39 项并行通过；
- `thread-shutdown` 打印 passed 后命中已知 CTest 4 秒边界；
- 串行复测两次均为 0.05 秒通过；
- 逻辑覆盖 40/40。

游戏：

- 2774 translation units；
- 1 require compilation；
- 只编译 `runtime_facade.cpp`；
- precise ABI 通过；
- EXE SHA-256：
  `2EF06CED49588E45A203905B0A0963FC284261734EFE7B898CFF394945F8F960`。

### 52.4 第一轮候选的 NVIDIA/OpenGL 异常

目录：

`_build/novagc-gameplay-major-growth128-clean-230s-20260726`

该轮 8/8、错误 0、cadence Full 0，但 app 96～138 秒出现 100～628 ms
帧间隔，不能因 GC 指标改善而忽略。

Haxe：

- `Stage.render.slow total_ms=571`；
- 多个 100～628 ms update/draw 间隔；
- 邻近 Young pause 约 8～11 ms、concurrent 约 1～3 ms。

ETW 四个精确窗口：

- 主线程最大离核 125～539 ms；
- render thread 同时离核 131～402 ms；
- render thread 栈集中在 `nvoglv64.dll`；
- 另见 `dxgkrnl.sys`、`dxgmms2.sys`、DXGI；
- 没有 Full、stall、fallback、emergency；
- GC heap 比基线更低。

结论：这是 NVIDIA/OpenGL/系统调度异常样本，不是门禁引发的 GC 堆积。
仍必须复测，不能只靠归因决定保留。

### 52.5 第二轮干净正式复测

为避免诊断 I/O 干扰，同一 EXE、同一场景、同一 WPR 契约重新运行，期间
不实时扫描 CSV/ETL：

`_build/novagc-gameplay-major-growth128-clean2-230s-20260726`

完整性：

- 8/8；
- exit 0；
- GC 151、frame 195、process 778、hardware 235；
- ETW 2,666,528,768 bytes；
- Haxe/native/Windows 错误 0；
- crash/hang/dump 0；
- stall/timeout/emergency/fallback 0。

Major：

| cycle | kind | Young since Full | Major growth | reason |
|---:|---|---:|---:|---|
| 8 | Full | 7 | startup | `explicit_request` |
| 136 | Young | 128 | 53.19 MiB | `cycle_growth_wait` |
| 150 | Young | 142 | 61.75 MiB | `cycle_growth_wait` |
| 151 | Young | 143 | 91.04 MiB | `cycle_growth_wait` |

自动 cadence Full 为 0，SoftMax Full 为 0。

共同 50～210 秒：

| 指标 | 64 MiB | 128 MiB clean2 |
|---|---:|---:|
| Full | 1 | 0 |
| update mean/P5 | 144.587/111.2 | 145.561/107.4 |
| update worst P95/P99/max | 25/33/200 ms | 26/32/43 ms |
| draw mean/P5 | 144.684/111.1 | 145.206/109.0 |
| draw worst P95/P99/max | 26/33.92/162 ms | 26/33/40 ms |
| pause mean/P95/max | 9.053/11.464/13.053 ms | 8.737/11.249/13.354 ms |
| concurrent mean/max | 3.312/207.915 ms | 1.514/4.207 ms |
| allocation | 12.267 MiB/s | 12.085 MiB/s |
| process CPU | 15.386% | 15.168% |
| Private Bytes | 1302.039 MiB | 1293.435 MiB |
| Working Set | 1089.173 MiB | 972.356 MiB |
| GC heap | 488.970 MiB | 457.850 MiB |
| CPU Performance | 122.490% | 122.537% |

双方都无 Full 的 cycle 9 后 20～160 秒窗口中，Young pause、concurrent、
CPU、Private Bytes 和 GC heap 没有系统性回退。P5 和局部 max 有运行方差，
但 P95/P99 基本同级。

### 52.6 当前决策

保留默认 128 MiB：

1. cycle 136 低收益 Major 被直接阻止；
2. explicit/promotion/SoftMax 安全兜底未删除；
3. 目标测试、逻辑 40/40、1/2774 和 precise ABI 通过；
4. 两轮候选都没有堆安全回退；
5. 第二轮在同 CPU Performance 下消除 Major 邻近 200/162 ms 尖峰；
6. 第一轮严重回退已经由 Haxe/ETW 独立定位为 NVIDIA/OpenGL。

空间清理候选为 worker=1 和第一轮异常候选的四个 ETL/ETLX，共
8,736,674,919 bytes（8.137 GiB）。删除在执行前被策略拦截，没有文件被
部分删除；保留列表已经精确定位，当前 D 盘可用 22.587 GiB。

完整证据：

`_build/novagc-gameplay-major-growth128-clean2-230s-20260726/EVIDENCE_SUMMARY.md`

## 53. 后续长期计划

### 53.1 第一优先级：Major 成本/压力自适应

128 MiB 是实测校正后的静态收益下限，不是最终调度器。下一阶段：

1. 记录每次自动 Full 的回收字节、回收比例和每毫秒回收字节；
2. 记录 mark/relocation wall time、workers 和邻近帧影响；
3. 结合 SoftMax headroom、allocation rate 与预测耗尽时间；
4. 增加 CPU headroom/硬件 performance state；
5. 预测 Major deadline，而不是继续叠加固定周期和常量；
6. promotion debt、显式请求和内存紧急仍可绕过收益门禁。

### 53.2 第二优先级：Major 专用 worker

全局 worker=1 已否决。若继续该方向：

1. 分离 Young 与 Major worker 配置；
2. 用当前 CPU headroom 和预计 Major work items 选择 1/2 workers；
3. 对比 concurrent wall time、主线程样本、P95/P99/max 和回收吞吐；
4. wall time 延长或 Young 被影响时立即回退。

### 53.3 第三优先级：Young、启动和应用层长帧

分别处理：

- 启动 cycle 8 Full；
- 启动 cycle 9 Young；
- ResultsScreen 后 cycle 150/151 大 cohort；
- 稳态 Young pause 长尾；
- NVIDIA/OpenGL 周期性全进程离核；
- ResultsScreen 同步图表构建约 15～41 秒。

应用层/驱动收益不得记作 ZGC 收益。

### 53.4 测量可重复性

1. frame CSV 增加歌曲位置/阶段标记；
2. 保留 CPU Performance、GPU 和窗口状态；
3. 以 cycle 9 或 song-start 为相位锚点；
4. 同时报告 mean、P5、low、P95/P99/max；
5. 单轮驱动异常必须 ETW 归因并复测；
6. 不用单轮高 FPS 推翻多轮 GC 直接证据。

### 53.5 执行规则

1. 只优化 ZGC，不恢复 Immix 修改，不处理 Freeplay/BPM；
2. 每个风险修改前建立精确恢复点；
3. 优先增量编译，只有公共 ABI/PCH 变化才合并一次必要全量构建；
4. 每轮先目标测试，再逻辑 40/40，最后一次必要游戏构建；
5. 每轮至少 180 秒，并由 Haxe state 确认同场景；
6. 强制抓取 Haxe 日志、Haxe 异常栈、原生线程栈、堆/GC、帧率、
   CPU/内存/线程、CPU Performance、GPU、窗口状态、Windows 事件和
   hang dump 状态；
7. 同时报告分配率、堆组成、GC phase、trigger reason 和硬件状态；
8. 不因单个指标改善掩盖其他回退；
9. 删除前验证绝对路径严格位于 `_build`，只删除已有摘要且可再生成的
   ETW/ETLX/NGENPDB；
10. 保留解析文本、CSV、JSONL、日志、摘要、恢复点和增量构建缓存。
