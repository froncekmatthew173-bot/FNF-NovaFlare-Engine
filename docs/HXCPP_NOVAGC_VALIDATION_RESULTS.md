# hxcpp NovaGC 验证记录

## 当前结论

NovaGC 已具备精确类型描述符、影子根、TLAB、分代复制、卡表、并发老年代标记、弱引用/ephemeron/finalizer、句柄与 pinned 对象、跨 legacy/Nova 堆桥接等主干能力。Windows 项目可以通过自定义 Haxe/hxcpp 工具链完整编译并启动。

当前实现仍处于双堆过渡期。`Array`、`String`、`Dynamic`、匿名对象和部分 native/CFFI 对象仍由 legacy hxcpp 管理，因此真实游戏和合成基准的性能受跨堆写屏障、根集合、pin 和 legacy 扫描显著影响。该限制不能被当作最终性能结果。

## 备份

- 初始备份：`D:\game\NovaFlare-backups\20260712-002120`
- 已批准重写基线：`D:\game\NovaFlare-backups\20260712-005805-approved-rewrite-baseline`

## 2026-07-12 关键故障与修复

### 原生身份对象被移动

- `GLObject` 及其子类不能移动，否则 native OpenGL/CFFI 会保留旧地址。
- `BitmapData` 及其子类同样具有 native 身份，移动后曾在 `BitmapData.getTexture` 触发访问冲突。
- 编译器现在将上述类型族放入 legacy/non-moving 区域。

### 间接调用函数漏报参数根

- 旧生成逻辑依赖 legacy `gc_stack` 标记。
- `BitmapData.__drawGL` 本身不直接分配，但调用链可以分配并触发 NovaGC；`renderer`、`source` 和 `this` 因此曾在移动回收后失效。
- 生成器现在根据 Nova safepoint 分析保护接收者和对象参数，不再依赖 legacy `gc_stack`。

### 每帧 `hxSehException` 导致约 1 FPS

表现：

- 第三轮 minor GC 后，`flixel.tweens.misc.VarTween.update` 第 54 行开始每帧触发并捕获 `hxSehException`。
- CrashHandler 吞掉异常后下一帧重试，造成持续接近 1 FPS。

第一现场：

- 故障指令位于 legacy Immix raw mark 路径。
- 一个 Nova 对象经 `Dynamic`/匿名 legacy 容器进入无类型 `MarkAllocUnchecked`。
- legacy collector 将 Nova 对象前 4 字节误当作 Immix allocation flags，并将对象所在 64 KiB 页的首 DWORD `0xA5422560` 误当作 `BlockId`，随后越界访问 `gBlockInfo`。

修复：

- `MarkAllocUnchecked` 在读取任何 Immix 元数据前调用 `NovaGCMarkObjectIfOwned`。
- 属于 Nova 的对象立即通过跨堆桥接保活/固定并返回，禁止进入 legacy Immix header/block 解释路径。
- typed `MarkObjectAlloc` 与 untyped/raw `MarkAllocUnchecked` 现在都具备 Nova 所有权防线。

修复后对照：

- 连续运行 10.3 分钟。
- 完成 4 轮 minor GC。
- 第四轮：`roots=482921`、`pinned=53848`、`objects=346717`、Nova committed `100 MiB`。
- `hxSehException` 计数始终为 0，窗口始终响应。

### OpenFL TextEngine 单次空引用

- 修复后 10 分钟样本记录到一次 `Null Object Reference`。
- 栈顶为 `openfl.text._internal.TextEngine.getLayoutGroups` 第 1513 行，调用来自开发者 FPS/GraphMonitor 文本布局。
- 它没有形成 VarTween 那样的逐帧异常循环，进程仍响应。
- 该问题需要独立抓取第一现场；不能与已修复的 raw Immix mark 问题混为一谈，也不能据此宣称最终稳定门禁全部通过。

## 诊断产物

- 1 FPS 完整堆转储：`artifacts/novagc/one-fps-full-heap.dmp`（约 1.08 GiB）
- 故障复现 GC 日志：`artifacts/novagc/one-fps-repro.gc.log`
- 故障复现应用日志：`artifacts/novagc/one-fps-repro.stdout.log`
- raw mark 修复后 GC 日志：`artifacts/novagc/raw-mark-fix.gc.log`
- raw mark 修复后应用日志：`artifacts/novagc/raw-mark-fix.stdout.log`
- 影子根修复日志：`artifacts/novagc/shadow-roots-trace.*.log`

## 已执行测试

- NovaGC bootstrap ABI/runtime：通过。
- NovaGC exact mark/TLAB：通过。
- 自定义 Haxe 生成器跨堆测试：`cross_heap_bridge=passed`。
- 手工完整 Windows 构建及链接：通过。
- `lime test windows`：完整编译、链接和自动启动通过；raw mark 修复后已再次执行。
- 带 GC trace 的真实应用：4 轮 minor GC、10.3 分钟、无 SEH。

## 性能基准结果

`test/nova_gc` 的完整混合堆基准在 20 分钟内未完成，但进程始终响应：

- 负载为 432 万个 `Node`，每个 Node 含 legacy `Array<Int>` payload。
- 20 分钟时 private bytes 约 637 MiB。
- 结果判定为“稳定但性能超时”，不能算性能通过。
- 主瓶颈是数百万 legacy Array/Nova Node 边、跨堆根和 pin，而不是纯 Nova 分配路径。

下一性能阶段必须优先把 `Array`、匿名对象/Dynamic 存储和 String 生命周期迁入新对象模型，之后才有资格用该基准评价 NovaGC 核心算法的上限。

## 尚未完成的门禁

- 独立定位并消除 TextEngine 第 1513 行的单次空引用。
- 完成不含 legacy Array 的纯 Nova 帧延迟基准，记录 mean/P95/P99/max。
- 迁移 Array/Dynamic/String 后重新运行完整混合负载。
- 最终候选版本执行最长 20 分钟真实交互 soak；任何 SEH、Null Object、窗口无响应或内存无界增长均不通过。
