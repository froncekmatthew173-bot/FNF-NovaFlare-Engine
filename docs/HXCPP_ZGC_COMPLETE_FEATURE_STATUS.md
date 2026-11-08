# NovaFlare 新 hxcpp：完整功能、实现状态与后续路线汇总

> 汇总日期：2026-07-13  
> 当前状态：按用户要求暂停生成、编译、启动和 GC 改写  
> 当前实现：从零编写、对外仍名为 `hxcpp` 的单堆精确低停顿运行时  
> 最终目标：完整替代原 hxcpp，在真实 Haxe/Lime/OpenFL 游戏中实现现代精确、并发、分代、区域化和帧预算感知 GC

## 1. 结论

当前仓库中的新 `hxcpp/` 已经不是在旧 hxcpp GC 上打补丁，也不是继续使用 Immix 或 LegacyBridge。它是具有新对象 ABI、新引用模型、新 Haxe C++ 代码生成分支、新 CFFI 边界和新 GC 内核的单堆运行时。

已经实现并经过单元测试验证的主要技术包括：

- 完全精确的对象描述符扫描。
- 显式精确根帧与静态根。
- 64 位彩色引用和可原子更新的引用槽。
- region、object-start bitmap、TLAB 和 bump-pointer 分配。
- SATB overwrite barrier、insertion barrier 和 relocation store forwarding。
- 单 worker 并发标记。
- 单 worker 并发对象复制与 forwarding。
- load barrier、forwarding chain 解析和引用槽 CAS 自愈。
- Strong、Weak、Pinned handle。
- weak、ephemeron、WeakMap、finalizer 和 resurrection。
- 新的 Object、String、Dynamic、Array、Class、Enum 和闭包运行时骨架。
- 自定义 `hxcpp_zgc` Haxe C++ 代码生成模式。
- Windows CFFI、文件、socket、进程、线程、zlib、TLS 等基础运行时。

但是，当前版本还不能称为完整 ZGC，也不能称为已经完成的 hxcpp 替代品，原因包括：

- 部分暂停阶段仍然包含随堆规模增长的工作。
- 外部 collect/compact 仍以同步 start 后立即 finish 的方式执行。
- 只有单 marker 和单 relocation worker。
- 没有真正的 nursery、Eden、Survivor、promotion 和 remembered-set。
- 没有最终机器级 stack map/register map，当前使用精确 RootFrame/RootScope。
- Haxe 语义、反射、CFFI 和 native ABI 仍未全部补齐。
- 自动调度、帧预算、完整遥测、OOM 恢复和跨平台尚未实现。
- 当前完整游戏可执行文件仍在启动阶段因 Dynamic 闭包元数据问题退出。

因此，当前最准确的定义是：

> 一个通过核心正确性测试的“单堆精确、彩色引用、并发标记/移动 GC + 新 Haxe C++ ABI”原型。

## 2. 实现边界与历史版本区分

### 2.1 已废弃的早期双堆 NovaGC

早期实现采用 legacy hxcpp 堆与 NovaGC 堆并存，并通过跨堆 root、pin 和 owner 表连接。该版本曾出现：

- legacy raw mark 把 Nova 对象头误解为 Immix 元数据。
- legacy 到 Nova 的永久 pinned root 无界增长。
- Nova 到 legacy 的 owner/edge 集合膨胀。
- `dirty cards × pinned objects` 二次复杂度。
- evacuation worklist 永久等待。
- 每帧 SEH 异常导致约 1 FPS。
- 空引用、地址失效以及 native 对象被错误移动。

这些记录只用于故障回归和设计反例，不代表当前实现能力，也不能把旧版测试结果算作当前单堆运行时的验证结果。

历史资料：

- `docs/HXCPP_NOVAGC_VALIDATION_RESULTS.md`
- `docs/NOVAGC_HEAP_LOG_DIAGNOSIS_2026-07-12.md`
- `artifacts/novagc/`

### 2.2 当前单堆新 hxcpp

当前权威实现位于：

- `hxcpp/include/`
- `hxcpp/runtime/gc/`
- `hxcpp/runtime/core/`
- `hxcpp/tests/`
- `toolchains/haxe-novagc/src/generators/gencpp.ml`

当前产物必须满足：

- 不链接旧 hxcpp GC。
- 不包含 Immix。
- 不包含 LegacyBridge。
- 不使用保守栈扫描。
- 不把未知整数或 native 内存误判为托管引用。
- 正式进程只有一个托管堆和一套对象所有权协议。
- legacy 代码只能作为独立基准，不能作为新运行时的 fallback。

当前 `tools/audit-hxcpp-zgc-no-legacy.ps1` 审计已经通过。

## 3. 当前 GC ABI

### 3.1 彩色引用

托管引用使用 64 位 `Ref`，低 2 位保存 view/color：

- `Marked0`
- `Marked1`
- `Remapped`

空引用恒为零。引用槽 `RefSlot` 为 8 字节，并通过 `std::atomic_ref<uint64_t>` 实现原子 load/store/exchange/CAS。

当前机制可以：

- 区分当前标记视图和重定位视图。
- 在 load barrier 中识别旧 view。
- 遍历完整 forwarding chain。
- 将引用按当前 good view 重染。
- 通过 CAS 把旧引用槽自愈成新引用。

这属于 ZGC 风格的彩色引用机制，但当前不是 OpenJDK ZGC 的多重虚拟地址映射实现。

### 3.2 对象头

当前对象头固定为 64 字节、16 字节对齐，包含：

- forwarding 引用。
- `TypeDescriptor` 指针。
- 总字节数和 payload 字节数。
- mark epoch。
- stable/relocating/relocated 状态。
- 稳定 object identity。
- published、pinned、large、finalizable、finalized 标志。
- age 字段。
- region index。
- pin count。

`age` 已经为未来分代设计预留，但目前尚未形成真正 promotion/tenuring 逻辑。

### 3.3 TypeDescriptor

每个托管类型通过精确 `TypeDescriptor` 描述：

- 稳定 type ID。
- 类型名称。
- super descriptor。
- 固定 payload 大小和对齐。
- 类型标志。
- 精确引用字段 offset 数组。
- variable-size/custom trace 函数。
- finalizer 函数。
- 动态字段读写函数。
- 动态索引读写函数。
- 动态字段名表。

继承类通过 `super` descriptor 链继续扫描基类字段，避免依赖保守扫描。

## 4. 精确 GC 状态

### 4.1 已实现的精确性

- 对象字段只按 descriptor 的引用 offset 扫描。
- Array、Dynamic、String、闭包等变长或特殊类型可以使用 custom trace。
- 普通整数、未知机器栈字节和 native 内存不会保活对象。
- 静态字段通过精确静态 root 表注册。
- C++ 局部引用通过 RootFrame/RootScope 显式声明。
- handle table 中的引用由 GC 精确扫描和更新。
- ephemeron、finalizer pending object 等特殊引用进入独立处理协议。
- derived/raw native pointer 不允许被当成普通永久托管根。

### 4.2 当前精确根实现的限制

当前生成器在函数序言、参数、局部变量、异常值和构造过程生成 `RootFrame/RootScope`。这已经是精确 GC，因为 GC 知道每一个引用槽，不扫描未知栈字节。

但它还不是最终高性能方案：

- 尚未生成机器级 stack map。
- 尚未记录 register map。
- 尚未在每个 safepoint 记录寄存器中的引用。
- `mapId` 已有接口，但没有完整 stack-map table 后端。
- 大量显式 RootScope 会增加函数序言、栈空间和写入成本。

最终目标是把 RootFrame 保留为验证或不支持 stack map 平台的后端，Windows 主路径改为真正的 stack/register map。

## 5. 分配器与 region

### 5.1 当前已实现

- 默认 region 大小为 2 MiB。
- 默认 TLAB 大小为 64 KiB。
- 默认大对象阈值为 128 KiB。
- 每线程 TLAB 使用 bump-pointer fast path。
- TLAB refill 才进入共享 heap lock。
- 新 region 在发布到共享列表前先为创建线程保留首段 TLAB。
- object-start bitmap 使用原子位图，可以精确枚举 region 内对象头。
- 大对象进入 large-only region。
- relocation source 会使关联 TLAB 失效。
- 分配统计包括对象数、字节数、TLAB fast allocation、refill 和 region 数量。

TLAB 压力测试曾在 8 个 mutator 上完成 160,000 个唯一对象分配，其中绝大多数走 TLAB fast path。

### 5.2 当前未实现

- 动态 TLAB 尺寸。
- 每线程/线程组独立 nursery region。
- Eden 和 Survivor region。
- promotion reserve。
- pinned 专用 region/space。
- humongous page-run 管理。
- size-segregated large object recycle。
- 虚拟地址大范围预留。
- 按页 commit/decommit。
- NUMA node-local region。
- dead object 在 mixed-live region 内的细粒度 sweep/reuse。

当前只有整个 region 无 live object 时直接释放，或通过 relocation 搬走 live objects 后延迟退休旧 region。普通 mixed-live region 中的死亡洞不会立即复用。

## 6. Safepoint 与线程协调

### 6.1 当前已实现

- cooperative safepoint polling。
- 每个附着线程有独立 ThreadState。
- 单调递增 handshake epoch。
- 请求、确认和恢复在同一个 condition-variable mutex 协议中发布。
- GC 发起线程可以确认自己的 epoch，避免等待自己。
- native blocking 线程不阻塞 GC handshake。
- safepoint handshake 有超时诊断。
- detach 时拒绝仍带有 active exact roots 的线程退出。
- TLAB、root chain 和 native blocked 状态都绑定在线程状态中。

此前约 5 秒的丢通知伪停顿已经由 epoch + condition-variable 协议修复。

### 6.2 当前限制

- 没有每线程到达 safepoint 的延迟统计。
- 没有最慢线程及最后 safepoint 位置诊断。
- 没有操作系统级线程抢占 fallback。
- 热循环仍依赖生成器插入 polling。
- native 第三方代码必须正确进入 blocking/callback 协议，否则仍可能拖延握手。

## 7. 并发标记

### 7.1 已实现机制

- initial snapshot handshake。
- 单独 marker thread。
- SATB overwrite barrier。
- insertion/update barrier。
- concurrent mark 期间新对象 born-black/marked-live。
- mark queue。
- descriptor 引用 offset 扫描。
- custom trace。
- ephemeron fixpoint。
- finalizer pending graph 保活。
- weak handle 清理。
- remark handshake。

### 7.2 当前不满足最终低停顿要求的部分

`finishConcurrentMark()` 当前在 mutator 暂停期间执行：

- marker worker join。
- remark root 扫描。
- ephemeron fixpoint。
- finalizer discovery。
- weak handle 清理。
- region 对象活性统计。
- 空 region reclaim。
- retired region 清理。

其中 region/object 活性遍历会随堆规模增长，因此当前还没有达到“正常 GC 的 STW 阶段不与整个堆线性相关”的最终约束。

此外，外部 `__hxcpp_collect()` 当前调用 `startConcurrentMark()` 后立即调用 `finishConcurrentMark()`。它虽然使用 marker thread，但没有自动跨多个游戏帧维持并发标记窗口，实际运行时仍可能把大部分标记等待时间放在同步调用中。

## 8. 并发重定位

### 8.1 已实现机制

- 标记完成后选择无 pinned live object 的 source region。
- source region TLAB 失效。
- relocation worker 在 mutator 运行期间复制 live object。
- 新副本保留 type、identity、age、flags 和 mark epoch。
- 旧对象头发布 forwarding 引用。
- load barrier 跟随 forwarding chain。
- relocation 期间写旧 owner 字段时按 owner-relative offset 转发到新副本。
- 统计 forwarded stores。
- late pin 会取消整个 source region 本轮重定位。
- 结束握手修复精确 roots、static roots、handles、ephemerons 和 finalizer cells。
- source region 延迟到后续完整精确遍历后释放，降低 UAF 风险。

### 8.2 当前限制

- relocation source 选择在暂停期间遍历 region 和对象。
- 只有一个 relocation worker。
- `finishConcurrentRelocation()` 会先暂停 mutator，再等待 relocation thread join。
- 外部 compact 是 begin 后立即 finish，没有跨帧自动调度。
- 没有 mixed-live region 成本/收益评分。
- 没有 evacuation reserve。
- relocation destination allocation failure 的恢复协议不完整。
- 没有有界 work-stealing outstanding-work 终止协议。
- 没有 page remap。
- 没有 pinned 专用空间。

因此当前是“具备并发复制机制”，但还不是最终 ZGC 级有界暂停重定位器。

## 9. 写屏障

当前 store barrier 同时承担：

- SATB：覆盖旧值，防止 snapshot-at-the-beginning 丢对象。
- insertion/update：覆盖新值，保证新增边能进入标记队列。
- relocation forwarding：owner 已移动时把写入转发到新副本对应 offset。
- 正常引用编码与当前 view 着色。

生成器已经覆盖普通对象字段、String/Dynamic 引用部分、静态根和部分反射写入。

仍需完成：

- 所有构造期直接赋值审计。
- Array 扩容和 backing buffer 所有路径审计。
- native/CFFI 写入审计。
- Class/Enum/匿名对象/闭包捕获字段审计。
- card marking。
- remembered-set。
- dirty-card thread-local buffer。
- barrier batching 和去重。

## 10. Handle、Weak、Ephemeron 与 Finalizer

### 10.1 Handle

已实现：

- Strong handle。
- Weak handle。
- Pinned handle。
- handle index + generation，拒绝 stale handle。
- pin count 增减。
- relocation 后 handle slot 更新。
- scoped native pin 包装。

尚未实现完整独立类型：

- Soft handle/reference。
- Phantom handle/reference。
- 完整 persistent/scoped handle 分级 ABI。
- handle table 分片和无锁/低锁扩展。

### 10.2 Weak 与 Ephemeron

已实现：

- weak handle 不保活目标。
- remark 后清除死亡 weak target。
- ephemeron key 可达时 value 才被标记。
- ephemeron fixpoint。
- key 死亡时同时清除 key/value。
- Haxe WeakMap 基于 ephemeron 实现。

### 10.3 Finalizer 与 Resurrection

状态机包括：

- Registered。
- Pending。
- Running。
- Finalized。
- Cleared。

已验证：

- finalizer 恰好执行一次。
- 回调在 GC 外执行。
- 回调执行时对象临时固定。
- finalizer 抛异常不会破坏 GC 状态。
- pending finalizer 对象在 relocation 后仍能解析。
- finalizer 可以通过静态根复活对象。
- 复活根移除后对象可以在后续周期最终回收。

尚未实现：

- `zombie` 对外队列；当前 `__hxcpp_get_next_zombie()` 返回空。
- finalizer 每帧执行预算。
- finalizer backlog 遥测。
- finalizer 线程亲和性策略。
- soft/phantom reference 处理阶段。

## 11. 分代 GC 状态

当前版本尚未实现真正分代 GC。以下内容全部仍属于后续强制功能：

- Eden。
- Survivor。
- nursery copying collection。
- parallel minor evacuation。
- object age histogram。
- adaptive tenuring threshold。
- promotion reserve。
- promotion failure recovery。
- old-to-young card table。
- remembered-set。
- thread-local dirty-card buffer。
- remembered-set overflow 和去重。
- pinned/non-moving nursery 分流。

未来 minor GC 的目标是不扫描完整老年代，只处理：

- 精确 roots。
- nursery objects。
- remembered old-to-young edges。
- pinned/large/native 特殊集合。

## 12. Haxe 对象模型

### 12.1 已建立的基础类型

- `hx::Object`。
- 标准布局 `ObjectPtr<T>`。
- 托管 variable-size `String`。
- tagged `Dynamic`。
- typed `Array<T>`。
- `cpp::VirtualArray`。
- `Class`。
- `EnumBase`。
- 静态函数闭包。
- 成员函数闭包。
- 本地闭包基础类。
- 弱映射。

### 12.2 Array

已实现和测试：

- primitive array。
- object reference array。
- String array。
- Dynamic array。
- custom trace 精确扫描引用元素。
- 动态扩容。
- push/pop/shift/unshift 等基础操作。
- 动态索引访问。
- iterator。
- typed array 到 VirtualArray 转换。
- `Array<double>` 到 `Array<Dynamic>`/VirtualArray 的元素转换。

仍需：

- 全部 hxcpp Array API 行为对齐。
- 所有泛型转换组合。
- 排序、拼接、切片和迭代边界压力测试。
- backing buffer pin 和 native blocking 安全。
- 扩容期间 derived pointer/root 验证。

### 12.3 String

已实现：

- 托管可变长字符串对象。
- literal string 支持。
- UTF-8 解析与验证。
- 常用比较、拼接、查找和转换。
- String 引用字段在写屏障后复制非引用元数据。

仍需：

- 与 Haxe/hxcpp 全部 UTF-8/UTF-16 语义对齐。
- Unicode 边界和无效序列完整矩阵。
- 字符串驻留、缓存和短字符串优化评估。

### 12.4 Dynamic

已实现：

- null、Bool、Int、Int64、Float、String、Object、Function、native pointer 等 tag。
- null 到 Bool/数字/String/ObjectPtr 的 Haxe 兼容转换。
- 动态字段和索引分派。
- 函数调用入口。
- 引用槽和非引用 tag/metadata 分离。

当前最近阻塞正是某个生成 setter 只复制了 Dynamic 引用槽，没有复制 tag metadata。生成器源码已加入 `copyNonReferenceFrom()`，但暂停时尚未重新生成完整项目。

### 12.5 Class、Enum、闭包和反射

已建立：

- Class 对象骨架。
- scalar/object/array class 分类。
- EnumBase、tag 和 arguments。
- static/member/local function 基础闭包。
- 动态方法 descriptor。
- Haxe 实例方法的动态反射返回闭包。

仍需完整补齐：

- 动态 Class 构造参数。
- 所有 Enum 动态构造和反射入口。
- 闭包捕获对象图的完整 descriptor。
- 方法 override/inline/native 特殊情况。
- `Reflect`、RTTI、metadata 和匿名对象全部行为。
- 异常展开期间闭包、参数和局部根生命周期。

## 13. 自定义 Haxe C++ 生成器

`toolchains/haxe-novagc/src/generators/gencpp.ml` 已增加独立 `hxcpp_zgc` 模式。

当前模式会生成：

- 每个托管类的 `TypeDescriptor`。
- 引用字段 offset。
- super descriptor 链。
- dynamic field get/set/index 函数。
- 实例方法动态反射闭包。
- 参数和局部精确根。
- 构造对象临时根。
- 构造期 pin/publish。
- 字段写屏障。
- String/Dynamic 非引用元数据复制。
- 静态 root 注册。
- 循环 safepoint。
- 异常变量 root。

该模式不会生成：

- 旧 `__Mark`。
- 旧 `__Visit`。
- 旧 GC vtable。
- legacy allocation。
- 保守栈扫描入口。

未来仍需：

- 完整 stack map/register map 输出。
- safepoint table。
- derived pointer 的 base + offset 表达。
- 所有直接赋值和容器写入的 barrier 覆盖。
- 更细的 liveness analysis，减少不必要 root scope。
- 优化生成代码体积和编译时间。

## 14. CFFI 与 Native ABI

### 14.1 已实现

- Prime CFFI 注册和 thunk。
- classic CFFI primitive 0～5 参数调用。
- `hx_cffi` 符号导出和函数解析。
- CFFI value token。
- call scope。
- `AutoGCRoot`。
- persistent root create/query/destroy。
- string、number、bool、array、anonymous object 和 buffer 转换。
- native pointer wrapper。
- CFFI field ID 和动态字段访问。
- CFFI finalizer 接口。
- Windows `.ndll`/DLL 装载。
- Prime primitive 注册表。

### 14.2 当前限制

- Lime 所需全部 CFFI 函数尚未逐项验证。
- classic CFFI 当前只覆盖 arity 0～5。
- native callback thread attachment 未覆盖所有第三方入口。
- CFFI raw managed pointer 只允许在 call scope 内使用，但所有外部库尚未完成审计。
- native 库永久保存对象地址时必须改用 handle 或 scoped pin。
- GL、音频、视频、Discord、Lua、VLC 等 native identity 类型仍需逐个验证。
- Linux/macOS 动态库加载尚未实现。

## 15. 系统运行时与标准库支持

当前 Windows 运行时已经包含或正在补齐：

- 文件读写、seek、tell、EOF、标准输入输出。
- 目录、路径、stat、环境变量和工作目录。
- 系统时间和 CPU time。
- IPv4/IPv6 socket。
- connect、bind、listen、accept、select、timeout 和 blocking mode。
- Windows process、stdin/stdout/stderr pipe、kill 和 exit code。
- Haxe thread、message queue。
- mutex、lock、TLS 和 deque。
- memory get/set/reinterpret。
- 日期和时区基础函数。
- `std::regex` 正则后端。
- zlib inflate。
- Mbed TLS 3.6.6 SSL、证书和 key 基础接口。
- embedded resources。
- DLL/NDLL primitive loading。

当前风险和缺口：

- socket/file/process blocking I/O 中有路径在 `BlockingScope` 内保存 Array backing 裸指针；移动 GC 下必须 pin 或改用稳定 native buffer。
- native stack trace 尚未完整实现。
- `std::regex` 与原 hxcpp/PCRE 语义可能有差异。
- SSL/CFFI 回调线程协议仍需压力验证。
- 非 Windows 分支大量为 unsupported 或空实现。
- 部分兼容 GC API 当前是 no-op，包括 `__hxcpp_enable()` 和旧调参接口。

## 16. 构建系统

当前新 hxcpp 自身是独立 haxelib，包含：

- `haxelib.json`。
- 新 `run.n`。
- `HxcppZgcBuild.hx` 构建前端。
- CMake 严格运行时构建。
- GCC/Clang `-Wall -Wextra -Wpedantic -Werror`。
- 对生成 C++ 的对象级增量缓存。
- `.o`、`.d`、`.rsp` 依赖缓存。
- 8 个 Haxe 调度线程并行调用独立 g++ TU 编译。
- 原子 `.building` 标记。
- 独立最终链接阶段。

当前完整 Windows 项目已经成功生成并链接过数千个 translation units，产出：

- `export/release/windows/obj/ApplicationMain.exe`

当前可执行文件约 791 MB，说明 debug 信息和生成代码体积仍需后续优化。

## 17. 当前测试状态

### 17.1 2026-07-13 暂停前重新执行结果

CTest：16/16 通过，总耗时约 0.76 秒。

覆盖：

1. bootstrap exact GC。
2. concurrent mark。
3. bootstrap relocation。
4. strong/weak/pinned handles。
5. concurrent relocation。
6. TLAB。
7. static roots。
8. ephemeron。
9. finalizer/resurrection。
10. generated ABI。
11. String。
12. Dynamic。
13. Array。
14. late pin。
15. CFFI。
16. WeakMap。

no-legacy 审计：通过。

### 17.2 这些测试尚不能证明的内容

- 真实游戏性能。
- 真实游戏 P99/P99.9 暂停。
- 多小时或 20 分钟交互稳定性。
- Lime/OpenFL 完整 Haxe 语义。
- 外部 NDLL 全面兼容性。
- 音频、视频、GL、Discord、Lua 等 native 生命周期。
- 多 worker 并发正确性。
- OOM、commit failure 和 promotion failure。
- 跨平台行为。

## 18. 当前项目启动阻塞

### 18.1 已经依次解决的问题

- Lime `hx_cffi` 装载失败。
- Haxe 实例方法没有进入动态反射 descriptor。
- `Dynamic` null 到可空 Bool/数字/String/Object 的转换错误。
- `Array.iterator` 缺失。
- typed Array 到 VirtualArray 的转换错误。
- Matrix3 初始化时 `Array<double>` 到 Dynamic array 的 `bad_cast`。
- metadata/RTTI/static resource root 的部分生成问题。

### 18.2 当前最新错误

当前可执行文件启动时输出：

```text
uncaught Haxe/C++ exception: Dynamic value is not callable
```

GDB 已定位到 `flixel.util.FlxPool.get()` 调用 `_constructor()`。

根因：

- `_constructor` 是 tagged `Dynamic`。
- 旧生成 setter 只通过写屏障复制 `mPtr`。
- setter 没有复制 Dynamic tag 和非引用元数据。
- 因此函数对象引用存在，但目标 Dynamic tag 仍为 Null。
- 调用时被判定为“Dynamic value is not callable”。

生成器源码已经修改为在写屏障后调用：

```cpp
_constructor.copyNonReferenceFrom(_hx_v);
```

但用户要求暂停时，完整 Haxe 重新生成正在运行，已被终止。因此当前工作区处于：

- 生成器源码：已修复。
- 已生成 C++：仍包含旧 setter。
- 已链接 exe：仍包含旧错误。
- 恢复时必须先完整重新生成，再编译和链接。

## 19. 当前性能判断

现在不能宣称新 hxcpp 已经比正式 hxcpp 更快，也不能宣称无卡顿，原因是：

- 应用尚未完成启动。
- 没有进入菜单、设置界面和谱面。
- 没有真实帧时间数据。
- 没有新单堆版本的 20 分钟稳定性测试。
- collect/compact 仍有同步 finish。
- remark、region scan 和 relocation selection 仍有堆规模相关暂停路径。
- 没有分代 minor GC。
- 没有多 worker。
- 没有自动帧预算调度器。

16 个核心测试只证明当前覆盖范围内的正确性，不代表真实性能或完整兼容性。

最终目标不能用“平均暂停下降”单独定义，必须同时检查：

- mean frame time。
- P50/P95/P99/P99.9 frame time。
- 最大单帧停顿。
- GC initial mark/remark/relocation flip 暂停。
- update FPS 和 draw FPS。
- 超出 60/120/144/165/240 FPS 帧预算的次数。
- heap used/reserved/committed。
- private bytes/RSS。
- 碎片率和回收收益。
- native/audio/video 线程受干扰程度。

## 20. 后续强制功能路线

### 阶段 A：恢复当前启动链

1. 使用新 Haxe 编译器完整重新生成项目。
2. 验证所有 Dynamic setter 复制 tag metadata。
3. 增量编译并完成最终链接。
4. 启动应用并继续捕获第一异常。
5. 直到窗口、菜单和基础状态能稳定运行。

### 阶段 B：补齐完整 Haxe 语义

1. Class 动态构造。
2. Enum 全部语义。
3. 闭包捕获和成员函数反射。
4. 匿名对象。
5. 完整 RTTI/metadata。
6. 异常展开根协议。
7. 泛型 Array 全操作面。
8. String Unicode 边界。
9. Dynamic 全部转换和运算。
10. 反射字段和动态索引完整行为。

### 阶段 C：CFFI/native 完整化

1. 对照 Lime 所有实际 primitive 补齐 `hx_cffi` 表。
2. 验证 Prime 和 classic CFFI。
3. 所有 callback thread 自动 attachment。
4. 所有 blocking I/O pin/handle 审计。
5. GL、音频、视频、Discord、Lua、VLC 等 native identity 对象协议。
6. 禁止 native 永久保存未注册托管裸指针。
7. 建立 native handle、scoped pin 和 derived pointer API。

### 阶段 D：真正机器级精确根

1. 定义 stack map 格式。
2. 定义 register map 格式。
3. 每个 safepoint 输出 live reference map。
4. derived pointer 记录 base object + offset。
5. 异常展开与 callback trampoline map。
6. debug verifier 对比 RootFrame 与 stack map。
7. Windows 主路径切换到 stack/register map。

### 阶段 E：自动并发 GC 调度

1. 自动触发 initial mark。
2. 并发标记跨多个游戏帧执行。
3. 增量 remark assist。
4. relocation selection 移出长暂停。
5. concurrent relocation 跨帧执行。
6. relocation flip 只保留有界根修复。
7. allocation debt 和 mutator assist。
8. loading/interactive/idle/background/memory-pressure 模式。

### 阶段 F：多 worker 与 work stealing

1. 动态 worker 创建。
2. configured/created/active/sleeping/running 状态。
3. 并行 mark。
4. 并行 remark 辅助。
5. 并行 evacuation。
6. 并行 reclaim、清零和统计。
7. work-stealing deque。
8. 可证明 outstanding-work 终止协议。
9. worker 超时和异常退出恢复。
10. P/E core、processor group 和 NUMA 感知。

### 阶段 G：真正分代 GC

1. Eden region。
2. Survivor region。
3. copying nursery。
4. parallel minor evacuation。
5. age histogram。
6. adaptive tenuring。
7. promotion reserve。
8. promotion failure recovery。
9. card table。
10. thread-local dirty-card buffer。
11. exact remembered-set。
12. remembered-set 去重和 overflow。
13. pinned/non-moving nursery。

### 阶段 H：高级 region 和内存管理

1. mixed-live region scoring。
2. fragmentation histogram。
3. evacuation reserve。
4. pinned 专用 space。
5. large/humongous space。
6. page-run allocator。
7. size-segregated recycle。
8. 虚拟地址预留。
9. commit/decommit。
10. 后台清零。
11. OS 内存归还。
12. NUMA first-touch/node-local 分配。

### 阶段 I：Weak/Finalizer 完善

1. soft reference。
2. phantom reference。
3. zombie queue。
4. resurrection 多轮回归。
5. finalizer budget。
6. finalizer backlog 遥测。
7. finalizer 线程亲和性。
8. shutdown finalizer 协议。

### 阶段 J：遥测和帧预算

1. GC reason。
2. initial mark/remark/relocation flip 时间。
3. worker 利用率。
4. safepoint per-thread latency。
5. TLAB/nursery/promotion 统计。
6. card/remembered-set 统计。
7. region fragmentation。
8. large/pinned memory。
9. P50/P95/P99/P99.9/max histogram。
10. JSON/CSV 输出。
11. Tracy timeline。
12. 与 update/draw FPS、frame time 和进程内存关联。
13. 开发者 HUD。

### 阶段 K：OOM 与故障恢复

1. OOM emergency mark/reclaim/compact。
2. promotion failure。
3. relocation allocation failure。
4. OS commit/decommit failure。
5. safepoint 随机延迟。
6. worker 异常退出。
7. queue overflow。
8. finalizer 抛异常和再注册。
9. native callback 竞态。
10. 无法恢复时明确失败，不静默损坏内存。

### 阶段 L：跨平台

1. Windows 完成并作为第一性能基线。
2. Linux GCC/Clang、pthread、madvise。
3. Android NDK、ARMv7/ARM64、大小核和低内存回调。
4. macOS Mach 内存、hardened runtime。
5. iOS native resource/finalizer 线程要求。

### 阶段 M：最终游戏门禁

1. `lime test windows` 完整生成、编译、链接和启动。
2. 主菜单。
3. 设置界面。
4. 资源加载。
5. 实际谱面。
6. 多次切歌。
7. 暂停和恢复。
8. 状态切换。
9. 退出并重新进入。
10. update/draw FPS 门槛。
11. GC/heap/frame telemetry 关联。
12. 每个候选版本单轮最长 20 分钟稳定性测试。

以下任一情况均判定失败：

- SEH。
- Null Object Reference。
- Dynamic/closure/Array/String 地址失效。
- 窗口无响应。
- update/draw FPS 持续低于门槛。
- 最大单帧停顿超过预算。
- 内存无界增长。
- handle/finalizer/weak 表无界增长。
- native callback 或阻塞线程死锁。

## 21. “零卡顿”目标的技术解释

在具有操作系统调度、native 驱动、音视频线程和 cooperative safepoint 的真实程序中，无法对所有硬件和所有外部代码给出数学意义上的绝对零暂停保证。

可实现并必须验证的目标是：

- 正常 GC 不出现堆规模线性 STW。
- initial mark、remark 和 relocation flip 受严格时间预算约束。
- 标记、复制、reclaim、统计、清零和 decommit 尽量并发。
- minor GC 在目标帧预算内完成。
- P99.9 和最大帧停顿达到肉眼不可感知范围。
- GC worker 不抢占音频和实时线程。
- 任何 GC 退化都能通过遥测明确感知，而不是只看平均 FPS。

最终是否“无感”必须由真实游戏数据判定，不能由实现名称或平均暂停推断。

## 22. 备份与恢复点

现有关键备份：

- 初始备份：`D:\game\NovaFlare-backups\20260712-002120`
- 已批准重写基线：`D:\game\NovaFlare-backups\20260712-005805-approved-rewrite-baseline`
- 旧尝试回滚：`D:\game\NovaFlare-backups\20260712-220000-rollback-fresh-zgc`
- handle 阶段：`D:\game\NovaFlare-backups\20260712-223829-hxcpp-zgc-mark-relocation-handles`
- relocation/TLAB 阶段：`D:\game\NovaFlare-backups\20260712-230747-hxcpp-zgc-concurrent-relocation-tlab`
- Haxe frontend 阶段：`D:\game\NovaFlare-backups\20260713-004506-hxcpp-zgc-haxe-frontend`
- CFFI 阶段：`D:\game\NovaFlare-backups\20260713-020035-hxcpp-zgc-cffi-descriptor-prime`
- native thread ABI 阶段：`D:\game\NovaFlare-backups\20260713-040820-hxcpp-zgc-native-thread-abi`
- 已知完整链接基线：`D:\game\NovaFlare-backups\20260713-111300-hxcpp-zgc-full-link-tls`

最后一个备份是已知完整链接基线，但不包含之后全部启动语义修复。恢复时应优先继续当前源码；只有出现无法启动且无法定位的破坏性回归时，才从最近验证备份恢复并重新应用后续已验证补丁。

## 23. 暂停点与恢复操作

当前暂停点：

- Haxe 生成器中的 Dynamic setter metadata 复制已修改。
- 完整项目重新生成被用户暂停并已终止。
- 不存在后台 Haxe/hxcpp 构建进程。
- C++ 核心 16/16 测试通过。
- no-legacy 审计通过。
- 当前生成输出和生成器源码不完全同步。

恢复后必须按以下顺序执行：

1. 确认 Docker Haxe 编译器容器和自定义 Haxe binary 状态。
2. 完整重新运行 `release.hxml -D hxcpp_zgc -D no-compilation`。
3. 验证生成的 `FlxPool._hx_set__constructor` 包含 `copyNonReferenceFrom(_hx_v)`。
4. 运行新 hxcpp 增量构建和最终链接。
5. 从 `export/release/windows/bin` 工作目录启动应用。
6. 若仍退出，使用 GDB `catch throw` 捕获第一异常。
7. 继续按“一个语义缺口、一个回归测试、一次完整启动验证”的方式推进。
8. 在应用能稳定进入游戏路径前，不开始宣称 GC 性能结果。

## 24. 状态总表

| 子系统 | 状态 |
|---|---|
| 单堆无 legacy | 已实现、审计通过 |
| 精确 TypeDescriptor | 已实现 |
| 精确 RootFrame | 已实现 |
| 机器 stack/register map | 未实现 |
| 彩色引用/load barrier | 已实现 |
| TLAB/region | 已实现基础版 |
| 并发标记 | 已实现单 worker 基础版 |
| 并发重定位 | 已实现单 worker 基础版 |
| SATB/insertion/forwarded store | 已实现 |
| Strong/Weak/Pinned handle | 已实现 |
| Weak/Ephemeron/WeakMap | 已实现 |
| Finalizer/Resurrection | 已实现 |
| Zombie/Soft/Phantom | 未实现 |
| Eden/Survivor/Promotion | 未实现 |
| Card table/Remembered-set | 未实现 |
| Multi-worker/Work-stealing | 未实现 |
| Pinned 专用空间 | 未实现 |
| Large/Humongous 完整空间 | 部分实现 |
| 自动 GC 调度器 | 未实现 |
| 帧预算模式 | 未实现 |
| JSON/CSV/Tracy 遥测 | 未实现 |
| OOM emergency collector | 未实现 |
| Object/String/Dynamic/Array | 已实现基础主干，仍在补语义 |
| Class/Enum/Closure/Reflection | 部分实现 |
| CFFI/Prime/NDLL | 部分实现 |
| Windows native runtime | 部分实现 |
| Linux/Android/macOS/iOS | 未实现 |
| C++ 核心测试 | 16/16 通过 |
| no-legacy 审计 | 通过 |
| 完整项目编译链接 | 曾通过，当前生成结果待同步 |
| 应用启动 | 当前失败：Dynamic value is not callable |
| 设置界面/谱面 | 尚未到达 |
| 最长 20 分钟稳定性测试 | 当前单堆版本尚未执行 |
| 最终性能结论 | 尚无资格得出 |

