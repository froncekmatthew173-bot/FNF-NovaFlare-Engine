# NovaGC 堆日志诊断与修复记录（2026-07-12）

## 结论

用户报告的“帧数长期低于 10、明显只有一帧”确实主要由 GC 引起，并非普通游戏逻辑负载。完整日志定位到多个叠加问题：

1. legacy GC 曾按“出现过混合引用的类型”扫描全部 Nova 对象，单次暂停最高超过 3 秒。
2. legacy 数组槽地址被永久保存，数组扩容后形成悬空槽。
3. legacy→Nova 的永久 pinned 根和 Nova→legacy 的历史 owner 集合不断膨胀。
4. pinned region 被拆成单对象 chunk 后，`containsHeader()` 对每个弱句柄线性扫描全部 chunk，形成数十亿次比较。
5. dirty-card 扫描采用 card-major 双重遍历，理论复杂度为 `dirty cards × pinned objects`。
6. evacuation 的并行 worklist 存在偶发永久死锁，表现为应用无响应且所有线程 CPU 归零。
7. 普通 `lime build windows` 可能调用系统 Haxe，静默生成全 legacy 代码，导致测试结果失真。

## 已完成修复

- 修正精确根生成条件，可能 safepoint 的方法现在生成参数根。
- 在 legacy 原始标记入口识别 Nova 对象，避免把 Nova header 当作 Immix header 解析并触发访问违规。
- `PinScope` 改为栈式 pinned root frame，1,000,000 次作用域从约 128,963µs 降至约 7,000–13,000µs。
- legacy 数组槽不再永久注册槽地址，改为按唯一 Nova 目标保活，消除扩容后的悬空槽。
- Nova→legacy remembered set 从“整类型扫描”改成按对象 identity 的 dirty-owner 集合。
- legacy owner 标记接入 hxcpp 现有并行 mark workers。
- 每次 Nova collection 后批量清除死亡 owner 弱句柄；一次实测从 64,314 条降至 24,729 条，释放 39,585 条。
- 首次 nursery 阈值从 32MiB 调整到 8MiB，避免第一次回收前桥接表过度膨胀。
- handle resolve/release 增加单锁批量路径，避免逐句柄重复加锁。
- legacy GC 周期的临时 owner pins 改为跨线程周期容器，避免 MarkBegin/MarkEnd 线程不同造成泄漏。
- 禁止 legacy 标记期间自动嵌套 Nova collection。
- dirty cards 按 region 聚合，每个 region 只遍历一次 chunks。
- `containsHeader()` 的 pinned-chunk 查询由线性扫描改成有序二分。
- weak roots 使用 region fast path，不再对每个弱句柄做 chunk 成员搜索。
- pinned roots 在 STW 中直接设置 `ObjectPinned`，移除每轮十万级指针哈希表。
- 扫描后淘汰已不再包含 legacy edge 的历史 owner。
- 不可靠的并行 evacuation 改为显式实验开关；生产默认使用无数据竞争的顺序路径。
- 增加逐秒 update/draw FPS、帧耗时、应用内存、GC 内存日志。
- 增加 GC roots、legacy scan、heap 五阶段以及总暂停日志。
- 增加 `tools/build-novagc-windows.ps1`，强制定制 Haxe，并在构建后验证精确 ABI。

## 性能变化

### 修复前

- update FPS 常见 1–2，draw FPS 可降至 3–7。
- legacy full owner scan：约 1.6–3.0 秒。
- Nova minor：出现约 3.3–3.6 秒暂停。
- 首次 Nova minor：约 19.8 万 weak handles，约 244ms。
- 程序曾因 Nova header 被 legacy raw mark 误解析而持续抛出 SEH。

### 当前已验证版本（v20）

- 程序可正常启动并持续响应。
- 菜单 draw 通常约 230–240FPS；GC/legacy scan 时仍存在短时下降。
- Nova minor 总暂停常见约 74–166ms。
- heap setup 从最高约 64ms 降至约 8–12ms。
- weak-root 阶段从约 30–37ms 降至约 5–14ms。
- 不再出现 3 秒级 Nova minor。
- 默认顺序 evacuation 后，已观察运行中未再次出现并行 worklist 永久死锁。

当前仍未达到“无感 GC”。legacy→Nova pinned targets 会继续增长，legacy full GC 仍可能达到约 0.7–0.9 秒；这是下一阶段的首要结构性瓶颈。

## 已回退的危险实验

曾尝试在 minor 中完全跳过 old/pinned roots，只依赖 dirty cards。该方向符合标准分代 GC 设计，但当前生成代码和部分原生容器的写屏障覆盖尚不完整。实测约 13 秒后应用以 `0xc0000005` 退出，Windows Application Error 的 fault RVA 落在 Discord RPC/RapidJSON 附近。该优化已完整撤销，不能重新启用，除非先完成写屏障覆盖审计和压力测试。

并行 evacuation 也曾两次出现永久无响应；症状为 `novagc:minor` 已开始、没有 `novagc:done`、进程 CPU 增量为 0。当前仅可通过 `HX_NOVAGC_EXPERIMENTAL_PARALLEL_EVACUATION=1` 实验性启用，生产构建默认禁用。

## 关键日志与转储

- `artifacts/novagc/one-fps-full-heap.dmp`：约 1.08GiB 的低帧完整堆转储。
- `artifacts/novagc/low-fps-cpu.etl`：WPR CPU/stack 采样。
- `artifacts/novagc/true-novagc-v3.gc.log`：确认 legacy 全类型扫描是 1FPS 主因。
- `artifacts/novagc/parallel-owner-mark-v6.gc.log`：owner 并行标记基线。
- `artifacts/novagc/owner-cleanup-v7.gc.log`：批量死亡 owner 清理。
- `artifacts/novagc/grouped-dirty-cards-v10b.gc.log`：阶段日志确认 weak membership 才是 3 秒主因。
- `artifacts/novagc/binary-chunk-lookup-v11.gc.log`：chunk 二分后秒级暂停消失。
- `artifacts/novagc/prune-empty-owners-v12.gc.log`：owner 反向淘汰与一次并行 evacuation 死锁。
- `artifacts/novagc/minor-skip-old-v14.gc.log`：危险 old-skip 实验，已回退。
- `artifacts/novagc/direct-pin-headers-v17.gc.log`：当前性能基线。
- `artifacts/novagc/direct-pin-headers-v17.stdout.log`：当前逐秒 FPS/内存基线。
- `artifacts/novagc/edge-shadow-v18.gc.log`：精确 field-slot owner 覆盖率采样。
- `artifacts/novagc/edge-completeness-v19.gc.log`：逐 owner 边数量完整性校验。
- `artifacts/novagc/production-safe-v20.gc.log`：关闭影子模式后的生产安全基线。
- `artifacts/novagc/production-safe-v20.stdout.log`：生产安全路径 FPS/内存日志。

## 精确跨堆边影子实验

生成 setter 的写屏障已能够取得字段槽地址，并以 owner-relative offset 记录 object/raw edge kind。该功能默认关闭，仅在 `HX_NOVAGC_EDGE_SHADOW=1` 时采样，不参与对象存活决策。

第一轮采样中，约 97.2% 的 mixed owner 至少记录到一个精确 slot；但是增加“实际扫描 legacy 边数量必须等于精确 slot 数量”的严格校验后，full GC 中完全匹配的 owner 约为 25%。这说明构造期直接赋值、继承扫描、数组/容器和部分原生路径仍未覆盖。当前不能用影子表替换 owner `__Mark`；下一步必须把 offset 和 edge kind 固化进 TypeDescriptor，并覆盖构造和容器路径。

## 测试结果

- NovaGC bootstrap ABI/runtime：通过。
- 精确标记与 TLAB：通过。
- legacy↔Nova cross-heap bridge：通过。
- `lime build windows`：通过。
- 生成结果包含 `__novaType`、`Runtime::allocate(&__novaType)`、`HX_NOVAGC_WB`：通过。
- Windows 应用启动：通过。

## 下一阶段

1. 将写屏障从 owner/value/oldValue 扩展到 owner/slot/value/oldValue，记录稳定的 owner-relative field offset。
2. 在 TypeDescriptor 中区分 object、raw allocation、derived pointer 等 edge kind。
3. 建立精确的跨堆 edge table，让 legacy GC 直接标记实际 legacy targets，不再扫描整个 Nova owner。
4. 将 legacy→Nova 引用从永久 pinned-handle 过渡到可更新槽或稳定 handle cell，降低根数量与碎片。
5. 为所有生成赋值、数组扩容、原生容器、String/Dynamic/Function 路径做写屏障覆盖审计。
6. 重写 evacuation 为有界 work-stealing 队列，证明 outstanding 计数与终止条件，再恢复默认并行。
7. 加入 p50/p95/p99/p999 pause histogram、最大单帧停顿和每阶段预算告警。
8. 完成最长 20 分钟的菜单、切换状态、资源加载、实际谱面和退出重进稳定性矩阵。
