# 新 hxcpp 精确低停顿运行时：实现与验证状态

日期：2026-07-12

## 已由代码和可执行测试验证

- 根目录 `hxcpp/` 是公开名称为 `hxcpp` 的独立 haxelib；新运行时不包含、不链接旧 hxcpp GC、Immix、LegacyBridge 或保守扫描入口。
- 首版 ABI 已冻结：64 位彩色 `Ref`、可平凡搬移且通过 `std::atomic_ref` 原子访问的 8 字节 `RefSlot`、64 字节对象头，以及精确 `TypeDescriptor`。
- `RootFrame`/`RootScope` 只扫描显式声明的引用槽。测试把真实托管地址复制到普通整数成员和机器栈，确认伪指针不会保活对象。
- 精确静态根、Strong/Weak/Pinned handle、pin 计数、ephemeron 条件可达 fixpoint 均进入标记、清理与重定位协议。
- finalizer 状态机覆盖 Registered/Pending/Running/Finalized/Cleared；不可达对象图会保留到回调完成，回调在 GC 外执行且临时固定。已验证恰好一次调用、异常隔离、重定位后的 pending finalizer、静态根复活，以及复活根移除后的最终回收。
- region bump allocation、TLAB、原子 object-start bitmap、large-only region 已实现。8 个 mutator 共分配 160,000 个对象时，156,856 次走 TLAB fast path；全部地址唯一，bitmap 能精确枚举并回收全部死亡对象。
- 新 region 会先为创建线程保留第一段 TLAB 再发布，避免并发 refill 取得同一首地址。
- concurrent mark 使用独立 marker 线程、SATB overwrite barrier、insertion barrier 和新分配黑色化；initial snapshot 与 remark 使用 cooperative safepoint handshake。
- safepoint 请求、确认和恢复使用单调 epoch，并在同一个 condition-variable mutex 下发布；已消除丢通知造成的约 5 秒伪停顿。
- load barrier 可解析 forwarding 链、按当前 good view 重染并 CAS 自愈引用槽。
- 两阶段 concurrent relocation 已实现：短握手冻结 source set 并失效相关 TLAB，mutator 继续运行时复制，旧 owner 写入按字段 offset 转发到新副本，结束握手修复精确根、handle、静态根、ephemeron 和 finalizer 后翻转 Remapped view。
- source region 延迟到后续完整精确遍历后释放；post-remark 新对象出生即标记存活，防止 relocation 选择和退休阶段出现 UAF。
- 自定义 Haxe C++ 生成器已有独立 `hxcpp_zgc` 模式；该模式生成 `::hx::gc` descriptor、精确参数/局部根、构造期发布与固定、owner-aware 字段写屏障和精确静态根，不生成旧 `__Mark/__Visit`、旧 GC vtable 或保守扫描代码。
- 外部 `__hxcpp_collect()`/compact 入口已改走 concurrent mark/concurrent relocation。同步发起收集的附着 mutator 会确认自己的握手 epoch，不会再等待自己造成 5 秒超时。
- 已实现首版精确 Haxe 值模型：标准布局 `ObjectPtr`、托管 variable-size `String`、首槽为引用的 tagged `Dynamic`，以及使用独立可变 backing buffer 的 `Array<T>`。String/Dynamic 的非引用元数据在 owner-aware 屏障后显式复制；引用数组由 custom trace 精确遍历。
- 构造参数中的 String/Object 会在分配前扎根；对象在 Haxe 构造体执行前发布并固定，因此构造函数内部再次分配不会遗漏半构造对象。
- 生成器不再对含字段继承类使用条件支持的 `offsetof`；descriptor 字段偏移通过真实默认构造实例和 pointer-to-member 按标准行为计算，基类引用由 descriptor `super` 链继续扫描。
- 真实 `Main.hx` 已生成、以 GCC 16 `-Werror` 编译并链接新运行时；继承对象、String、Dynamic、`Array<Int>`、`Array<Dynamic>`、数组扩容和静态根均跨越并发收集与重定位后通过值校验。
- C++20/GCC 16 构建通过；当前 CTest 13/13 通过：bootstrap、concurrent mark、bootstrap relocation、handle、concurrent relocation、TLAB、static root、ephemeron、finalizer、generated ABI、String、Dynamic、Array。
- concurrent relocation 强制保留旧 owner 跨越复制窗口并验证 forwarded store；100 轮内部阶段计时不再出现 5 秒握手停顿。ephemeron、finalizer、TLAB 和 relocation 均有重复压力运行。
- `tools/audit-hxcpp-zgc-no-legacy.ps1` 可审计新包是否意外依赖旧 GC 符号。

## 仍未实现，不能视为完整 ZGC 或项目替代品

- 当前并发标记和并发复制各只有一个 worker；work stealing、多 worker 自适应并行、并行 remark 和拓扑感知调度尚未实现。
- mixed-live region 的收益/成本评分、relocation failure 恢复、并发复制失败时的可继续分配协议尚未完成。
- pinned 专用 space、TLAB 动态尺寸、虚拟地址预留、page commit/decommit、NUMA 策略尚未完成。
- soft/phantom reference 尚未实现；weak、ephemeron、finalizer 和复活协议已经实现。
- 分代 nursery、survivor、promotion、card table、remembered set 尚未实现。
- `hxcpp_zgc` 生成分支和 Object/String/Dynamic/Array 主干已经接通，但完整 Haxe 语义尚未完成：Enum、Class、闭包、异常、反射、泛型数组全操作面、UTF-8/UTF-16 细节、线程和完整 stack-map 仍待迁移。
- CFFI、native blocking/callback、Bytes/hash、Lime build frontend 和外部库尚未迁移。
- 项目 haxelib 尚未切换；本轮尚未运行 `lime test windows`、完整游戏路径或最长 20 分钟稳定性测试。

## 当前验证边界

`collectBootstrap()` 和 `compactBootstrap()` 仅保留为精确对象图的正确性 oracle；应用外部入口已经使用 concurrent mark/relocation。当前显式 collect 仍同步等待 worker 完成，自动并发调度、多 worker 和暂停时间直方图尚未接入，因此还不能据此宣称达到最终低停顿预算。
