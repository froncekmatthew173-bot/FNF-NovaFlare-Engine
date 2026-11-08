# NovaFlare hxcpp GC 深度重写总纲

> 状态：设计评审稿，尚未据此开始大规模重写  
> 目标平台：Windows 为第一验证平台，随后覆盖 Linux、Android、macOS 与 iOS  
> 主要目标：以性能、低延迟和可移动精确对象模型为最高优先级，允许彻底破坏旧 hxcpp ABI、裸指针 CFFI 和保守栈扫描兼容性，将新运行时优化到可测量的工程瓶颈

## 0. 已批准的破坏式设计决策

本项目不再以兼容旧 hxcpp GC 为目标。新实现是新的 hxcpp 精确运行时，而不只是旧 `Immix.cpp` 的替代文件。

以下能力全部属于强制交付项，除非通过设计证明彼此存在根本冲突：

- 完全精确 tracing，不将任意整数或未知内存误判为对象引用。
- hxcpp 代码生成器生成类型描述符、字段引用位图和专用扫描函数。
- 每个 safepoint 具备精确 stack map，覆盖栈槽、寄存器、内部引用和派生指针。
- shadow stack 只能作为引导、验证或不支持 stack map 平台的临时后端，不能作为最终高性能默认路径。
- 新对象头和新对象 ABI，可加入 region、年龄、颜色、pin、forwarding 或压缩引用所需元数据。
- 线程本地 TLAB 与 bump-pointer 小对象分配。
- Eden、Survivor、年龄统计、自适应 tenuring 和 promotion reserve。
- 并行 copying nursery 与对象 evacuation。
- card table、线程本地 dirty-card buffer 和精确 remembered-set。
- 区域化老年代和 region 生命周期状态机。
- SATB 并发老年代标记，以及与分代屏障融合的写屏障。
- 短暂停 initial mark 与 remark，支持帧预算内的增量辅助工作。
- work-stealing 并行标记、疏散、reclaim、统计和 sweep。
- 分阶段或并发 sweep，mutator 可按需协助完成回收债务。
- 选择性 region evacuation 与压缩，而非每次全堆压缩。
- 独立 pinned object space，pin 必须可计数、可诊断并有生命周期。
- 独立 large/humongous object space，具备 page-run 管理、分级复用和 OS decommit。
- Strong、Weak、Pinned、Scoped、Persistent native handle table。
- native 扩展禁止永久保存未注册的托管裸指针。
- 精确 weak、ephemeron、zombie、resurrection 和 finalizer 管线。
- 帧预算感知调度器、GC debt、分配速率预测和暂停时间预测。
- loading、interactive、idle、background、memory-pressure latency hint。
- 完整 GC reason、阶段时间、线程利用率、堆状态和 Tracy/JSON 遥测。
- OOM emergency collector、promotion failure 恢复和故障注入路径。
- Windows 第一实现，之后提供 Linux、Android、macOS、iOS 的平台层。
- 单次最长 20 分钟的高强度稳定性与随机故障验证。

如果两项技术发生冲突，必须通过基准、正确性证明和设计记录决定取舍。例如：极短暂停与最低内存占用、对象压缩与大量 pin、并发标记与最低写屏障成本不能同时达到理论最优。取舍必须保留更符合实时游戏目标的方案，而不能为了开发简单删除强制能力。

## 1. 项目目标

本项目不是简单修改几个阈值，而是逐步替换 hxcpp 当前 GC 的分配、调度、标记、回收和线程协调实现，最终形成适合实时游戏的低延迟分代收集器。

“更好”必须由数据定义，至少同时考察：

- 单线程和多线程分配吞吐。
- 自动 minor GC 的平均、P95、P99、P99.9 和最大暂停。
- full GC 的平均、P95、P99 和最大暂停。
- 60、120、144、165、240 FPS 下的超帧次数。
- 游戏主线程、音频线程和视频解码线程受到的干扰。
- 工作集、private bytes、GC reserved、GC current 和峰值内存。
- 堆碎片率、大对象碎片率、回收收益和内存归还速度。
- 加载、切歌、游玩、暂停、返回菜单和退出时的稳定性。
- 弱引用、finalizer、CFFI、native pointer 和多线程对象图的正确性。
- Windows、Linux、Android、macOS、iOS 的一致性。

不能以牺牲正确性换取漂亮的平均值，也不能只降低 GC 次数却让单次最大停顿恶化。

## 2. 当前实现基线

当前实现是经过多次定制的 generational Immix，具有以下能力：

- Immix block/line 分配和回收。
- 分代标记与 remembered-set。
- stop-the-world 并行标记。
- 并行 reclaim、统计和后台清零。
- 大对象独立分配与 recycle。
- 保守栈扫描和 GC free zone。
- 弱引用、zombie 和 finalizer。
- moving/visit allocs 相关路径。
- CFFI 与 native allocation 统计。
- Windows、Apple、Linux、Android 进程内存统计。

当前已观察到的问题：

- minor 和 full GC 仍然完整暂停所有受管线程。
- 首次 GC 原本会预创建最多 32 个工作线程。
- GC 最大暂停配置不是硬时间预算，只是间接调节阈值。
- 大对象分配路径可能同步触发 stop-the-world。
- 分代决策主要依赖固定填充率和粗略存活率估计。
- 若干公开配置接口尚未真正接入执行路径。
- `Immix.cpp` 职责过多，修改风险与审查成本很高。
- 游戏加载阶段直接关闭 GC，重新开启时可能集中偿还 GC 债务。
- 缺少稳定的 P99.9、碎片率、GC 原因和各阶段耗时遥测。

当前初步基准结果仅作为起点：

- hxcpp 自带测试 444/444 assertions 通过。
- 合成负载可复现约 18–23 ms 的默认长尾。
-实验性阈值组合在部分轮次将典型长尾降低到约 12–15 ms。
- 按需创建线程可降低首次 GC 的建池成本。
- 数据仍有操作系统调度噪声，不能作为最终结论。

## 3. 新运行时边界与必须淘汰的旧约束

新实现允许重新定义：

- 对象头、类型指针、mark state、年龄、pin 和 forwarding 表示。
- `hx::Object`、Dynamic、Array、String、Class 和闭包的内部布局。
- GC API、CFFI root API、线程注册和 native callback 协议。
- 生成代码的字段写入、数组写入、函数序言/结尾和 safepoint 形式。
- 异常展开、反射、虚调用和临时值的 root 表示。
- weak、zombie 和 finalizer 的内部数据结构。
- large allocation 与 native memory pressure API。

必须淘汰：

- 对未知机器栈进行全范围保守扫描的默认模式。
- 将任何形似堆地址的整数视为强引用。
- native 代码永久保存未注册 `hx::Object*` 的做法。
- offset root 和无法验证基对象的内部裸指针协议。
- 依赖对象永不移动的第三方扩展写法。
- 设置成功但不影响执行路径的伪配置接口。
- 在普通大对象分配路径任意同步触发 full GC。
- 长时间完全关闭 GC 后一次性偿还全部债务的游戏集成方式。

新边界要求：

- 所有托管引用必须由 stack map、对象描述符、静态 root 表或 handle table 描述。
- 派生指针必须记录基对象和偏移，GC 移动后重新计算。
- native 代码通过 handle 或有作用域的 pin 访问托管对象。
- 所有可能触发 GC 的位置必须是显式 safepoint 或具备对应 map。
- 无法迁移的扩展需要重写，不能迫使新 GC 回退到全局保守模式。
- 旧运行时仅作为独立可执行文件和性能/行为对照，不作为新实现内部 fallback。

## 4. 总体架构方向

目标架构为“完全精确 + 分代 + 区域化 + 并行复制 + SATB 并发标记 + 可增量 + 帧预算感知”的收集器：

1. 新代码生成器提供精确 type map、stack map、register map 和 safepoint table。
2. 线程本地 TLAB/nursery 分配负责绝大多数短命小对象。
3. minor GC 并行复制或提升年轻对象，不扫描完整老年代。
4. card table/remembered-set 精确记录老对象到年轻对象的引用。
5. 老年代使用新的 region heap，不继承旧 Immix block/line 架构约束。
6. 老年代标记拆分为短暂停初始标记、SATB 并发/增量标记和短暂停重标记。
7. sweep、region reclaim、统计、清零和 OS decommit 尽量并行或分阶段执行。
8. 高碎片 region 执行选择性 evacuation；pinned region 独立管理。
9. 大对象区独立管理，不在正常资源分配路径随意触发同步 full GC。
10. native 交互统一经过 handle table 或 scoped pin。
11. 调度器根据帧预算、分配速率、存活率、GC 债务和内存压力做决策。
12. 紧急 OOM 路径保留强制完整标记、疏散、回收和明确失败能力。

## 5. 实施阶段

### 阶段 0：冻结与可恢复性

- 保存主仓库、`.haxelib` 和 hxcpp 子仓库完整备份。
- 记录 Haxe、hxcpp、Lime、OpenFL、MSVC、Windows SDK 版本。
- 记录当前 hxcpp commit、编译参数、CPU、GPU、内存和电源计划。
- 为每个稳定阶段创建可恢复 tag 或补丁包。
- 自动生成变更清单和关键源文件哈希。
- 保证任何实验都能恢复到已验证基线。

退出条件：备份文件数、关键哈希和恢复演练均通过。

### 阶段 1：建立可信基准与遥测

- 将当前 `nova_gc` 合成测试扩展为多个负载模型。
- 使用 hxcpp 高精度单调时钟，不使用低分辨率系统时间。
- 分离冷启动、首次 GC、稳态 minor、full GC 和退出回收。
- 记录每次 GC 的原因，而不仅是耗时。
- 记录 setup、等待线程、扫描 roots、扫描 stacks、mark、weak/finalizer、sweep、reclaim、large sweep、defrag 和 resume 各阶段耗时。
- 记录参与 GC 的工作线程数、有效工作时间、空转、偷取失败和唤醒次数。
- 记录 collection 前后 GC current、reserved、large、process RSS/private bytes。
- 记录 nursery 分配量、提升量、存活率和 promotion failure。
- 记录 remembered-set/card 数量、重复条目和扫描成本。
- 记录 block 填充率、fragged rows、最大 hole、空 block 和归还 OS 的内存。
- 记录每帧 GC 预算消耗和剩余 GC 债务。
- 输出机器可解析的 JSON/CSV，同时支持 Tracy timeline。
- 为性能测试固定进程优先级、电源计划、CPU 热身和测试轮数。
- 使用中位数、MAD、bootstrap confidence interval，避免只比较单次最大值。
- 保存原始样本，不只保存汇总值。

负载模型至少包括：

- 大量短命小对象。
- 中等存活率对象。
- 高存活率对象。
- 深链、宽图和循环引用。
- 大 Array、String、Bytes 和匿名对象。
- 大对象和 native memory 高频变化。
- 多线程同时分配。
- 多线程建立跨线程对象图。
- 弱引用和 finalizer 风暴。
- 频繁创建/销毁线程。
- 极深栈和内部指针。
- GC 禁用后重新启用。
- 低内存和分配失败。
- 单次最长 20 分钟的高强度 soak test，并通过不同随机种子和负载组合重复执行。

退出条件：相同版本多轮测试的误差范围可解释，并能稳定识别已知退化。

### 阶段 2：建立独立的新精确运行时骨架

旧 `Immix.cpp` 保持只读，仅用于生成基线。新建完全独立的运行时模块：

- `GcConfig`：环境变量、API 配置、默认值和校验。
- `GcTelemetry`：事件、时间、计数器和导出。
- `GcScheduler`：回收原因、模式和预算状态机。
- `GcThreadPool`：worker 生命周期、任务队列和同步。
- `GcSafepoint`：线程暂停、注册、GC free zone 和恢复。
- `GcMarker`：root/stack/object 标记。
- `GcRememberedSet`：跨代引用。
- `GcNursery`：年轻代。
- `RegionHeap`：region、page、commit/decommit、reclaim 和 fragmentation。
- `LargeObjectSpace`：大对象与 native memory 压力。
- `GcFinalization`：weak、zombie 和 finalizer。
- `GcPlatformMemory`：平台内存信息与 OS page 操作。

要求：

- 新旧运行时构建产物和符号空间隔离，禁止新实现静默调用旧 GC 核心。
- 先定义新对象 ABI、类型描述符、stack map 格式、handle ABI 和 GC phase 状态机。
- 在新代码生成器接通前，使用最小 bootstrap 编译器样例验证新运行时。
- 明确锁顺序和每个字段的所有权。
- 为共享状态标注 atomic、锁保护或 stop-the-world 前提。
- 删除或实现无效配置接口，禁止“设置成功但没有效果”。

退出条件：新运行时可独立编译最小 Haxe 程序，并能执行精确 root 扫描、分配和显式回收；旧运行时基线仍可单独构建对照。

### 阶段 3：重写 GC 工作线程池

- 按实际需要创建 worker，不预建平台最大线程数。
- 区分 configured、created、active、sleeping 和 running worker。
- 允许不同任务使用不同并发度。
- 对小任务使用主线程或少量 worker，避免唤醒成本高于收益。
- 对标记、reclaim、统计、清零和 visit blocks 分别建立任务粒度模型。
- 使用动态 chunk size，避免末尾长尾 worker。
- 实现 work stealing 或共享无锁/低锁任务队列。
- 减少全局锁竞争和 semaphore 风暴。
- 为 worker 设置稳定命名，便于 Tracy/调试器识别。
- 研究 Windows processor group 和超过 64 逻辑处理器情况。
- 检测 P/E core 或 NUMA 时提供拓扑感知配置。
- 避免 GC worker 抢占音频实时线程。
- 支持自动线程数、显式线程数和任务级线程上限。
- 对线程创建失败、worker 异常退出和同步超时做安全降级。
- 验证 1、2、4、8、16、32 worker 的收益曲线。

退出条件：首次 GC 不再创建无用线程；稳态暂停不劣于基线；无死锁和 worker 泄漏。

### 阶段 4：重写 safepoint 与线程协调

- 明确 cooperative safepoint 状态机。
- 避免在 GC free zone 中等待永远不会到达的线程。
- 将线程状态转换设计为可验证的有限状态机。
- 减少暂停请求后的全局锁持有时间。
- 记录每个线程到达 safepoint 的延迟。
- 定位最慢线程及其最后安全点。
- 在 String、Array、循环和 native 边界补充合理安全点。
- 避免在极短热循环中每次检查全局原子变量。
- 使用低成本 epoch/polling 机制。
- 保证线程注册、退出和池化复用不会留下悬空 stack context。
- 对阻塞 I/O、音频、视频和第三方 native 线程建立明确约束。
- 增加 safepoint timeout 诊断，但 release 模式不能误杀正常线程。
- 验证 GC 发起线程本身处于各种锁状态时不会死锁。

退出条件：暂停 setup 时间显著降低，并通过随机线程启停和 GC free zone 压力测试。

### 阶段 5：线程本地快速分配路径

- 分析旧分配路径只用于建立对照；新路径不继承 Immix local allocator 结构。
- 为小对象提供线程本地 bump pointer fast path。
- 将 limit check 保持为少量分支。
- 避免每次小对象分配访问共享 cache line。
- 保持对象对齐和 container header 正确。
- 将零填充策略与对象初始化需求结合，避免重复清零。
- 对固定大小和常见大小建立 size class 或快速 line 获取路径。
- 批量从全局 heap 领取 TLAB/nursery region。
- 线程退出时安全归还未使用区域。
- 处理超大对象、特殊对齐和 pinned object 的独立空间分流。
- 对分配计数和遥测使用线程本地累积后批量合并。
- 研究 huge page、VirtualAlloc reserve/commit 和 page fault 成本。

退出条件：单线程和多线程小对象分配吞吐提升，且没有增加 GC 长尾或内存浪费。

### 阶段 6：真正的 nursery/年轻代

- 为每个 mutator 或线程组建立 nursery region。
- 定义 Eden、survivor 和 promotion 流程。
- 对可移动对象采用 copying nursery，快速丢弃死亡对象。
- 对 pinned、内部指针或不可安全移动对象提供 non-moving nursery 路径。
- 精确处理 stack map 描述的普通引用、派生指针和寄存器引用。
- 禁止 ambiguous root；无法描述的 native 引用必须迁移为 handle 或 scoped pin。
- 设计年龄位或 side metadata，避免扩大对象头。
- 根据对象年龄、大小和存活历史决定提升。
- 避免 promotion avalanche。
- 提供 promotion reserve，防止 minor GC 中途失败。
- promotion failure 时安全升级为 full GC。
- 动态调整 nursery 大小，而不是固定字节数。
- 根据分配速率、存活率、目标暂停和可用内存计算下一轮容量。
- 为加载场景和游玩场景使用不同控制目标，但不硬编码游戏类名。
- 大 Array/Bytes 根据大小与存活特征决定直接进入老年代或大对象区。
- 验证对象地址敏感的 native/CFFI 代码。

退出条件：多数短命对象无需进入老年代；minor GC 扫描工作与老年代规模弱相关。

### 阶段 7：重写写屏障和 remembered-set

- 审计所有 Haxe 生成代码的字段、数组和 Dynamic 写入路径。
- 保证老对象指向年轻对象时必定记录。
- 采用 card table、object remembered-set 或混合方案进行基准比较。
- 使用每线程 dirty-card buffer，减少共享锁。
- 批量合并并去重 remembered entries。
- 使用 card 粒度扫描避免大量重复对象条目。
- 对大 Array 采用范围/card 标记。
- 避免重复写同一 card 带来的 cache line 抖动。
- 为 concurrent marking 预留 SATB 或 incremental-update barrier 模式。
- 明确 native/CFFI 修改托管引用时的 barrier API。
- debug 模式随机执行完整堆验证，发现漏 barrier。
- 加入 generational invariant checker。

退出条件：minor GC 无漏标；barrier 热路径开销在可接受范围；remembered-set 不无限增长。

### 阶段 8：自适应 GC 调度器

- 所有 GC 必须携带明确 reason code。
- 区分 allocation failure、nursery full、large pressure、native pressure、explicit collect、compact、idle、shutdown 和 OOM。
- 根据近期分配速率预测 nursery 填满时间。
- 根据历史 bytes/ms 预测各阶段耗时。
- 根据目标帧预算决定当前允许执行的工作。
- 使用 EWMA、分位数和异常值保护，而不是只看上一次数据。
- 建立 GC debt：推迟的工作必须可见且受上限约束。
- 允许游戏提供 loading、interactive、idle、background 等 latency hint。
- hint 只能影响策略，不能破坏正确性或无限禁止 GC。
- 在空闲帧主动偿还债务。
- 在高内存压力时逐步提高回收积极度。
- 防止阈值震荡和 minor/full 模式频繁切换。
- 根据存活率自动调整 tenuring threshold。
- 根据碎片率决定是否需要 full reclaim 或 compact。
- 将最大暂停目标变成实际预算，而不是阈值预设名称。
- 在无法满足预算时记录原因和不可分割阶段。

退出条件：策略能适应短命、高存活、加载和低内存负载，不依赖单一魔法百分比。

### 阶段 9：增量与并发老年代标记

- 将 full mark 拆为 initial mark、concurrent/incremental mark、remark。
- initial mark 只完成必须在 STW 下完成的 root snapshot。
- mutator 运行期间由 worker 消化大部分标记队列。
- 选择并验证 SATB 或 incremental-update barrier。
- 每帧可在预算内执行一小段增量标记。
- remark 处理 barrier buffer、线程本地队列和新 root。
- 保证 finalizer、weak reference 处理顺序正确。
- 并发标记期间新分配对象采用明确的颜色/epoch 规则。
- 防止 floating garbage 无限制增加内存。
- 对 marker termination 使用可靠的全局终止检测。
- 处理线程进入/离开 GC free zone 时的 root snapshot。
- debug 模式与完整 STW mark 对照验证对象集合。
- 若平台或构建不支持所需 atomic，使用新运行时自身的精确 STW 算法后端，而不是旧保守 GC。

退出条件：老年代规模增长时，最长 STW 不再近似线性增长。

### 阶段 10：分阶段 sweep、reclaim 与清零

- 标记结束后只在 STW 中完成必要 metadata 交换。
- 将可安全并发的 sweep/reclaim 移到后台。
- block 状态必须能区分待 sweep、正在 sweep、可分配和已归还。
- mutator 获取 block 时可按需帮助完成 sweep。
- 使用 sweep cursor 和预算分段执行。
- 避免在单帧扫描所有大对象。
- free-list 重建分片化。
- 清零与实际分配时间结合，避免污染 cache。
- 后台清零线程应受内存带宽和帧负载限制。
- 避免后台 GC 与纹理上传、音频解码争夺带宽。
- 将空 block 按水位批量归还 OS。
- Windows 使用 VirtualFree/VirtualAlloc 或 decommit 策略进行对照。
- POSIX 使用 madvise 等可用机制。

退出条件：sweep/reclaim 不再形成大单块暂停，后台工作不会显著影响帧时间。

### 阶段 11：大对象区重写

- 明确定义 large object threshold，并基于平台/负载调优。
- 按大小分级 recycle，避免只匹配完全相同大小导致浪费。
- 使用 best-fit、segregated free list 或 page-run allocator 比较碎片。
- 对图像、音频和视频 buffer 的生命周期建立专门负载。
- 分离托管大对象和仅统计的 native memory。
- native memory pressure 只提交回收请求，不在普通分配路径任意同步 GC。
- 在安全点由调度器处理 soft pressure。
- 只有分配失败或硬内存上限才进入 emergency collection。
- 限制 recycle cache 总量和单对象上限。
- 大对象 sweep 分批进行。
- 对 pinned 大对象避免无意义移动。
- 支持内存映射或直接 OS page allocation 的实验后端。
- 处理整数溢出、超大分配和 32 位地址空间耗尽。

退出条件：大资源加载不再随机触发普通帧中的 full GC，碎片和峰值内存受控。

### 阶段 12：弱引用与 finalizer 管线

- 将 weak discovery、ephemeron fixpoint、finalizer enqueue 和执行分离。
- 明确弱键/弱值和 resurrection 语义。
- finalizer 不应在持有核心 heap lock 时执行用户代码。
- finalizer 分配对象时不能破坏当前 GC 状态。
- 支持 finalizer 队列预算，避免单帧执行过多析构。
- 对必须及时释放的 native 资源提供显式 dispose 建议。
- 检测 finalizer backlog 和长耗时 finalizer。
- 多线程 finalizer 必须有明确线程亲和性策略。
- 视频、音频和 GPU 资源释放若要求主线程，使用主线程队列。
- 重复 resurrection、finalizer 抛异常和进程退出路径必须测试。

退出条件：弱引用和 finalizer 压力测试无泄漏、重复释放、死锁和不可控暂停。

### 阶段 13：碎片整理与对象移动

- 先量化碎片，再决定是否 compact。
- 将 line/block fragmentation 与 large object fragmentation 分开统计。
- 优先回收高收益 block，不全堆盲目移动。
- 建立 evacuation candidate score。
- 对 pinned/ambiguous pointer block 排除移动。
- 为移动对象建立可靠 forwarding 信息。
- 所有 root、字段、数组、弱引用和 native registered pointer 必须更新。
- compact 使用额外空间上限，避免峰值翻倍。
- compact 可分阶段时研究增量 evacuation；无法安全实现则保持显式 STW。
- 用户显式 compact 和 OOM compact 与普通策略分开。
- 移动验证模式检查旧地址引用。

退出条件：只有在真实碎片收益足够时触发 compact，并能实际向 OS 归还内存。

### 阶段 14：缓存、NUMA 和底层内存优化

- 重排常用 metadata，减少 pointer chasing。
- 将只读、热写和跨线程字段分离 cache line。
- 避免 false sharing。
- 使用 prefetch 前必须有硬件计数器证据。
- 比较 bitmap、byte map 和压缩 metadata。
- 优化 mark stack chunk 大小和局部性。
- 对大数组扫描进行向量化或批处理。
- 研究 non-temporal zeroing，但仅在足够大区域使用。
- 使用 ETW、Windows Performance Recorder、Tracy 和硬件计数器分析。
- NUMA 系统按 first-touch 或 node-local 分配，验证跨节点标记成本。
- 禁止没有基准支持的微优化和平台专用汇编扩散。

退出条件：优化在多台机器上有统计显著收益，不只对单一 CPU 有效。

### 阶段 15：NovaFlare 游戏集成

- 替换简单 `GCManager.enable(false)` 的长期禁用策略。
- 增加 loading、interactive、idle 和 memory warning hint。
- 加载阶段允许后台/增量工作，但限制长 STW。
- 切歌前后分阶段清理资源，不在状态切换点集中 full GC。
- 帧循环末尾提供可选 GC budget tick。
- 记录实际帧时间和 GC timeline 的对应关系。
- 音频 callback 线程必须始终处于正确 GC free zone 或完全不接触托管对象。
- 视频解码线程、LuaJIT、Discord RPC 和资源预加载线程分别审计。
- HScript/Lua 动态对象生命周期建立专项测试。
- 资源缓存 clear、destroy、native release 和 GC 的顺序明确化。
- 根据显示刷新率动态计算预算，不假设固定 60 FPS。
- 提供开发者 HUD：最近 GC、P99、债务、nursery、old、large、RSS。
- release 模式关闭昂贵诊断但保留低成本计数。

退出条件：真实歌曲和资源场景的 P99/P99.9 帧时间优于当前版本，且无音频爆音和状态切换崩溃。

### 阶段 16：跨平台适配

- Windows/MSVC/x64 首先稳定。
- Windows 32 位验证地址空间和 header 限制。
- Linux GCC/Clang 验证 pthread、madvise 和栈边界。
- Android 验证不同 NDK、ARMv7、ARM64、大小核与低内存回调。
- macOS/iOS 验证 Mach 内存信息、线程栈和 hardened runtime。
- Apple 平台验证 Objective-C/原生资源 finalizer 线程要求。
- Emscripten 单线程和 pthread 模式分别提供安全降级。
- 主机线程数、page size、atomic 能力和栈方向不得硬编码。
- 不支持并发标记的平台自动使用新运行时的精确 STW 后端。

退出条件：各目标平台至少完成编译、启动、GC 压力和基本游戏流程测试。

### 阶段 17：故障注入与极限验证

- 在每 N 次分配后模拟失败。
- 模拟 worker 创建失败。
- 模拟 OS commit/decommit 失败。
- 随机延迟 mutator 到达 safepoint。
- 随机延迟或暂停 GC worker。
- 在标记、remark、sweep 和 finalizer 阶段注入线程切换。
- 强制 remembered-set buffer 频繁溢出。
- 强制 promotion reserve 耗尽。
- 强制 mark stack chunk 分配失败。
- 强制大对象 recycle miss。
- 模拟极低物理内存和 32 位地址空间压力。
- 使用 address sanitizer、thread sanitizer 或平台可用替代工具。
- 开启 HXCPP_GC_VERIFY、generational verifier 和移动验证。
- 对相同随机种子重复数千轮。
- 崩溃时保存 GC phase、epoch、线程状态和关键计数。

退出条件：所有可恢复故障安全降级；不可恢复 OOM 给出明确诊断而非静默内存破坏。

### 阶段 18：20 分钟稳定性与发布门禁

- 每个候选版本执行短基准、正确性、完整游戏启动和最长 20 分钟的 soak test。
- 快速迭代使用 2–5 分钟高强度测试。
- 稳定候选使用 20 分钟测试，覆盖多次切歌、资源循环、线程启停和显式/自动 GC。
- 通过多个随机种子重复 20 分钟以内的测试，观察泄漏、碎片、计数器溢出和罕见竞态。
- 任何单次自动化稳定性测试不得超过 20 分钟。
- 多机器覆盖 4、8、16、32 逻辑处理器。
- 覆盖低内存和高内存设备。
- 保存基线与候选原始数据。
- 性能退化超过阈值自动拒绝候选。
- 正确性失败、启动失败、应用无法打开、死锁或内存破坏立即回滚。
- 只有同时满足性能、内存和稳定性门禁才进入默认配置。
- 实验能力保留 feature flag；回退只能发生在新运行时内部算法之间，不能回退到旧 ABI 或保守扫描。

## 6. 具体性能优化清单

以下项目需要逐一验证，不保证全部启用：

- 按需 GC worker 创建。
- 每种任务独立并发度。
- 自适应任务 chunk。
- work stealing。
- 线程本地 mark buffer。
- 线程本地 dirty-card buffer。
- 批量 root 扫描。
- 批量 remembered-set 去重。
- 小对象 bump allocation。
- TLAB/nursery region。
- survivor region。
- 自适应 tenuring。
- promotion reserve。
- 大对象直接老年代分配。
- Array/Bytes 大小感知策略。
- allocation-rate prediction。
- pause-time prediction。
- GC debt。
- 帧预算 tick。
- idle collection。
- memory-pressure escalation。
- concurrent old marking。
- incremental remark preparation。
- 分阶段 weak processing。
- 分阶段 finalizer drain。
- concurrent/assisted sweep。
- lazy block sweep。
- 后台 reclaim。
- 按需清零。
- OS page decommit。
- size-segregated large recycle。
- 大对象分批 sweep。
- 碎片评分。
- 选择性 evacuation。
- pinned block 隔离。
- hot/cold metadata 分离。
- cache-line padding。
- NUMA-aware region。
- 平台拓扑感知 worker。
- 低成本 release telemetry。
- debug shadow marking。
- barrier invariant verification。
- safepoint latency tracking。
- slow-thread diagnosis。
- GC reason tracking。
- OOM emergency state machine。

## 7. 正确性测试矩阵

每轮核心改动至少验证：

- 空堆 full GC。
- 单对象存活与死亡。
- 深链、宽图、环和自引用。
- 静态 root、普通 root、offset root。
- 栈 root 和寄存器 root。
- 内部指针和疑似指针。
- container 与非 container allocation。
- String、Array、Bytes、Dynamic、匿名对象和闭包。
- 多线程同时分配和修改引用。
- 线程创建、退出和 allocator 复用。
- GC free zone 进入、退出和嵌套错误。
- 老到年轻、年轻到老和跨线程引用。
- remembered-set overflow。
- minor 后对象年龄和提升。
- full 后 mark epoch。
- weak key、weak value 和 ephemeron。
- zombie 与 resurrection。
- finalizer 分配、抛异常和再次注册。
- native root、CFFI callback 和第三方线程。
- large object allocate、recycle、free 和 realloc。
- explicit minor、major、compact、enable/disable。
- OOM 与 promotion failure。
- moving collector 与 pinned object。
- debug、release、telemetry、Tracy 构建组合。

## 8. 性能验收指标

最终数值必须在稳定基准后确定，初始方向如下：

- 不允许任何正确性测试退化。
- 小对象分配吞吐不得低于当前版本。
- 首次 GC 不得创建超过实际使用数量的 worker。
- 常见年轻代负载下 P99 minor pause 显著低于当前版本。
- P99.9 与最大暂停必须同时观察，不能只优化平均值。
- 真实游戏 60 FPS 下 GC 导致的超过 16.67 ms 帧显著减少。
- 120/144 FPS 下超预算帧也必须报告。
- 空闲和加载阶段应主动降低后续交互阶段 GC 债务。
- 稳态内存增长必须收敛。
- 允许用有限内存换低延迟，但必须设置明确上限。
- peak private bytes 和 RSS 不得无界增长。
- 后台 GC 不得造成明显音频爆音或持续 CPU 满载。
- 在低内存压力下必须优先保证正确释放和存活，而不是强守延迟目标。

## 9. 回滚条件

出现下列任一情况立即停止当前路线并恢复最近稳定点：

- 游戏应用无法正常打开。
- 启动后无窗口、闪退或无响应。
- hxcpp 正确性测试出现失败。
- 发生错误释放、use-after-free、double free 或堆损坏。
- 多线程死锁、活锁或 safepoint 永久等待。
- finalizer/weak 语义改变。
- CFFI/native 库出现地址失效。
- P99/P99.9 明显退化且无法由内存下降解释。
- 内存持续增长不收敛。
- 跨平台无法安全降级。
- 改动无法被隔离、审查或重复测试。

回滚后必须保留失败方案、数据和原因，防止重复走同一路线。

## 10. 技术瓶颈与不可抗力判定

“优化到技术瓶颈”不意味着无限堆叠复杂度。满足以下情况时，可认定某方向已接近当前约束下的瓶颈：

- 剩余最长暂停来自不可分割的精确 root snapshot、remark 或必须 STW 的系统操作。
- 第三方 native 组件拒绝迁移到 handle/pin ABI，且替换该组件超出项目控制范围。
- 第三方 native/CFFI 未提供写屏障或 root 生命周期信息。
- 操作系统线程调度、page fault 或驱动停顿主导长尾。
- 音频、GPU、文件 I/O 或 shader 编译成为主要卡顿来源，而非 GC。
- 进一步降低暂停需要不可接受地扩大对象头、stack map、代码体积或写屏障成本。
- 进一步并发化的 barrier 成本超过减少的暂停。
- 多轮统计显示收益落入测试噪声，且在多台机器上不可重复。
- 内存换延迟已经达到预设峰值上限。
- 复杂度、维护风险和故障概率超过可测量收益。

到达瓶颈时必须输出 flame graph/时间线、原始统计和受限原因，不能仅凭主观判断停止。

## 11. 交付物

- 本总纲及每阶段设计记录。
- 可恢复备份和稳定版本标签。
- 模块化 GC 源码。
- 合成基准、正确性测试、故障注入测试和 soak runner。
- 基线与每个候选版本的原始数据。
- Windows/Linux/Android/macOS/iOS 验证记录。
- GC 配置说明和安全默认值。
- Tracy/JSON/CSV 遥测接口。
- NovaFlare GC HUD 和帧时间关联工具。
- 已知限制、技术瓶颈与下一代对象模型建议。

## 12. 推荐执行顺序

实际执行严格遵循：

1. 冻结备份与恢复演练。
2. 完善基准与遥测。
3. 纯模块化拆分。
4. 工作线程池重写。
5. safepoint 状态机重写。
6. 线程本地快速分配。
7. nursery 与 promotion。
8. write barrier 与 remembered-set。
9. 自适应调度器。
10. 增量/并发老年代标记。
11. 分阶段 sweep/reclaim。
12. 大对象区。
13. weak/finalizer 管线。
14. 碎片整理与 OS 内存归还。
15. NovaFlare 帧预算集成。
16. 跨平台、故障注入和最长 20 分钟稳定性验证。
17. 多机器复测并确认技术瓶颈。

每个步骤都必须执行“基线对照 → 实现 → 正确性 → 压力 → 完整游戏 → 保留或回滚”，禁止多个高风险算法同时替换后才开始测试。
