# 原版 hxcpp 功能对照与新 runtime 完整实现总清单

> 建立日期：2026-07-14  
> 对照基线：`D:\game\superbackup\git`  
> 新实现：本仓库 `hxcpp/` 与 `toolchains/haxe-novagc/`  
> 目标：新 runtime 不复用旧 Immix/LegacyBridge，但必须逐项覆盖原版 hxcpp 对 Haxe、Lime、OpenFL、CFFI 和 native 库提供的可观察功能；在此基础上再实现精确、并发、着色指针、低停顿 ZGC 风格 GC。

## 1. 本文是后续工作的硬性清单

以后不能用“项目成功链接”“窗口还能运行”代替兼容性结论。每个原版能力只能处于以下状态之一：

- `E`：已经有等价实现，并有独立回归测试及真实程序验证。
- `P`：只有部分实现，或只在简单测试中通过。
- `N`：新源码中没有对应实现。
- `A`：存在同名 API，但尚未证明行为、异常、线程与生命周期语义等价。
- `R`：原技术不照搬，但必须由新机制提供等价或更强语义，例如保守栈扫描改为精确根/stack map。
- `X`：经过调用图、Haxe 标准库和目标平台审计后确认不适用于目标；必须写出证据，不能口头跳过。

完成条件不是把所有格子改成同一个字母，而是：所有适用于 Windows/Lime/OpenFL/NovaFlare 的项目均为 `E`；跨平台与可选子系统至少有明确实现或有证据的 `X`；任何公开 ABI 缺口均有测试、适配层或明确替代协议。

## 2. 对照基线的真实状态

备份仓库的 `haxelib.json` 标为 hxcpp `4.3.0`、binary version `48`，当前 Git 基线为：

```text
commit: 08778f53b40e02868a4b7aae151aaf6c788e526d
date:   2026-07-11
title:  Revert "Make GC low-latency defaults resource safe"
```

这份备份不是纯净上游快照：

```text
 M src/hx/gc/Immix.cpp
?? test/nova_gc/
```

因此对照分成两层：

1. hxcpp 4.3.0 runtime、本地库、CFFI、cppia、调试和构建系统的通用能力。
2. 这份备份在 `Immix.cpp` 中额外加入的 minor GC、并行/精炼线程、暂停预算、large refresh 等控制接口。

规模快照：

| 项目 | 原版备份 | 当前新实现 |
|---|---:|---:|
| runtime 公共头文件 | 56 | 15 |
| runtime C++/Objective-C++ 源文件 | 54 | 18 |
| 公共头中不同的 `__hxcpp_*` 名称 | 229 | 当前全部源码命中原名 91 个 |
| 原公共 `__hxcpp_*` 名称在当前全部源码中完全缺失 | 0 | 138 |
| 经典 CFFI API 表入口 | 100 | 名称命中 67，缺失 33 |

名称命中不等于语义已经正确。91 个同名入口仍需逐项进行签名、返回值、异常、线程、GC 屏障和 native 生命周期测试。

## 3. 原版 hxcpp 的完整模块边界

原版不是单独一个 GC。它同时承担：

1. Haxe C++ 对象 ABI 和生成代码所依赖的宏/模板。
2. Object、Dynamic、String、Array、Class、Enum、匿名对象、闭包与接口运行时。
3. 反射、RTTI、metadata、类注册、动态构造、动态字段与动态索引。
4. 异常抛出、调用栈、异常栈、调试器、采样 profiler、Telemetry 与 Tracy。
5. GC、线程注册、栈/寄存器根、弱引用、finalizer、对象身份及 native blocking 协议。
6. classic CFFI、Prime CFFI、NDLL/DLL 加载、root、buffer、abstract/kind 与 callback。
7. 文件、进程、Socket、Sys、日期、随机数、正则、zlib、SSL、SQLite、MySQL。
8. cppia 解释器/JIT、scriptable class、SLJIT 多架构后端。
9. Windows、Linux、Android、Apple、WinRT、Neko/static link 等平台层。
10. hxcpp 构建工具、Build.xml 条件系统、第三方库和生成产物打包。

新实现如果只重写 GC 而遗漏这些层，Lime/OpenFL 会以看似随机的 `x`、`textureScale`、闭包不可调用、空引用、状态切换卡死或 NDLL 崩溃表现出来。

### 3.1 原版 54 个 runtime 源文件/源文件族的映射

| 原版源文件或文件族 | 责任 | 当前对应 | 状态 |
|---|---|---|---|
| `src/Array.cpp` | ArrayBase、数组反射、转换与通用操作 | `runtime/core/array.cpp`、`include/hx/array.hpp` | `P` |
| `src/Dynamic.cpp` | boxed Dynamic、数值/Bool、动态运算与闭包基础 | `runtime/core/dynamic.cpp`、`hxcpp.h` | `P/R` |
| `src/Enum.cpp` | enum 参数、比较、字符串化、反射 | `include/hx/enum.hpp`、generated descriptors | `P` |
| `src/String.cpp` | String 分配、编码、hash、操作与转换 | `runtime/core/string.cpp` | `P` |
| `src/Math.cpp` | Haxe Math 与随机数 | `hxMath.h`、部分 runtime facade | `P` |
| `src/ObjcData.mm` | Objective-C 动态桥接 | 无 | `N` |
| `src/hx/Object.cpp` | 对象虚协议、字段、索引、比较、异常默认行为 | 分散在 `hxcpp.h`、dynamic/class runtime | `P/R` |
| `src/hx/Anon.cpp` | fixed/dynamic FieldMap、字段枚举/删除/toString | `runtime/core/cffi.cpp` managed/dynamic anon | `P` |
| `src/hx/Boot.cpp` | runtime boot、平台初始化、异常边界 | `runtime_facade.cpp`、`executable_main.cpp` | `P` |
| `src/hx/Class.cpp` | class registry、RTTI、construct/cast/fields | `runtime/core/class.cpp`、`class.hpp` | `P` |
| `src/hx/CFFI.cpp` | classic CFFI、root、buffer、abstract、call、field、blocking | `runtime/core/cffi.cpp` | `P`，33 个入口名缺失 |
| `src/hx/Date.cpp` | local/UTC/date/timezone | `runtime/core/native_date.cpp` | `P` |
| `src/hx/Debug.cpp` | stack frame、调用栈和调试元数据 | 只有生成的基础 StackContext | `N/P` |
| `src/hx/Debugger.cpp` | breakpoint/step/thread/stack-variable debugger | 无 | `N` |
| `src/hx/Profiler.cpp` | sampling profiler | 无 | `N` |
| `src/hx/Telemetry.cpp` | HXT frame/allocation/GC telemetry | 无 | `N` |
| `src/hx/TelemetryTracy.cpp` | Tracy zone/message/plot/thread 集成 | 无 | `N` |
| `src/hx/Thread.cpp` | Thread、Mutex、Lock、TLS、Deque、Condition、Semaphore | `runtime/core/thread.cpp` | `P` |
| `src/hx/Lib.cpp` | DLL/NDLL loader、路径、primitive registry | `runtime/core/native_lib.cpp` | `P` |
| `src/hx/RunLibs.cpp`/`NoFiles.cpp` | library main、debug file/class tables | `executable_main.cpp`/generated boot | `P/N` |
| `src/hx/StdLibs.cpp` | resources、print、parse、hash、field IDs、memory、random | `resources.cpp`、`memory.cpp`、`runtime_facade.cpp` 等 | `P` |
| `src/hx/Hash.cpp` | int/int64/string/object hash backing maps | 分散在匿名字段/WeakMap/生成标准库 | `N/P` |
| `src/hx/AndroidCompat.cpp` | Android libc/platform compatibility | 无 | `N` |
| `src/hx/gc/GcCommon.cpp` | 通用 GC facade/config/enable/freeze | `runtime_facade.cpp`、`gc/runtime.cpp` | `P/R` |
| `src/hx/gc/GcRegCapture.cpp` | 保守寄存器根捕获 | 用精确 RootScope，最终 stack/register map | `R/P` |
| `src/hx/gc/Immix.cpp` | allocator、major/minor、mark/move、weak/finalizer、线程池 | 全新 `runtime/gc/runtime.cpp` | `R/P` |
| `src/hx/cppia/*` | cppia VM、scriptable、JIT、builtin | 无 | `N` |
| `src/hx/cppia/sljit_src/*` | 多架构 JIT 汇编后端 | 无 | `N` |
| `src/hx/libs/std/File.cpp` | 文件 I/O | `native_sys.cpp` | `P` |
| `src/hx/libs/std/Process.cpp` | 子进程与 pipe | `native_sys.cpp` | `P` |
| `src/hx/libs/std/Random.cpp` | std random primitive | 部分 C++ random | `N/P` |
| `src/hx/libs/std/Socket.cpp` | Socket/DNS/select | `native_sys.cpp` | `P` |
| `src/hx/libs/std/Sys.cpp` | filesystem/env/path/time/system | `native_sys.cpp` | `P` |
| `src/hx/libs/regexp/RegExp.cpp` | PCRE2 regexp | `native_regex.cpp` 使用不同后端 | `P/R` |
| `src/hx/libs/zlib/ZLib.cpp` | zlib streaming API | `native_zlib.cpp` | `P` |
| `src/hx/libs/ssl/SSL.cpp` | TLS/cert/key/socket glue | `native_ssl.cpp` | `P/R` |
| `src/hx/libs/sqlite/Sqlite.cpp` | SQLite NDLL | 无 | `N` |
| `src/hx/libs/mysql/*` | MySQL protocol/client NDLL | 无 | `N` |

### 3.2 原版公共头文件族的映射

| 原版头文件族 | 内容 | 当前状态 |
|---|---|---|
| `Array.h`、`Dynamic.h`、`Enum.h`、`hxString.h`、`null.h` | Haxe 核心值/对象 ABI | `P/R` |
| `cpp/Pointer.h`、`Variant.h`、`VirtualArray.h`、`Int64.h`、`FastIterator.h` | C++ interop | `P` |
| `hx/Object.h`、`Class.h`、`Interface.h`、`Anon.h`、`Functions.h` | 对象、反射、接口、字段与闭包 | `P` |
| `hx/GC.h`、`Memory.h`、`GcTypeInference.h`、`StackContext.h` | GC ABI、根、分配和生成宏 | `P/R` |
| `hx/CFFI*.h` | classic/Prime/Neko/JS CFFI | `P/N` |
| `hx/Debug.h`、`Telemetry*.h` | 调试、stack、HXT、Tracy | `N/P` |
| `hx/Thread.h`、`Tls.h`、`OS.h` | 线程和平台抽象 | `P` |
| `hx/Macros*.h`、`GenMacro.hx`、`Scriptable.h` | Haxe 生成 ABI、scriptable/cppia | `P/N` |
| `hx/Native.h`、`ObjcHelpers.h`、`NekoFunc.h` | native/Apple/Neko bridge | `N/P` |
| `hx/StdLibs.h`、`StdString.h`、`StringAlloc.h` | 标准 runtime、字符串和 native buffer | `P` |

## 4. Haxe 对象模型与生成 ABI 对照

| 原版能力 | 原版实现位置/语义 | 当前状态 | 必须达到的验收 |
|---|---|---|---|
| `hx::Object` 基类 | `include/hx/Object.h`、虚调用、字段、索引、比较、class、toString、函数调用 | `P/R` | 所有生成类、native 类、闭包、数组、枚举均能通过统一对象协议；错误接收者要给出正确 Haxe 异常与栈 |
| `ObjectPtr<T>` | 类型转换、接口转换、空值、字段引用、索引引用 | `P` | 继承/接口/空值/交叉 cast/dynamic cast 差分测试 |
| `Dynamic` | 数值/Bool/String/Object/Int64/Pointer/Struct、运算、比较、调用 0～5/varargs | `P` | 对 Haxe 标准库建立转换、运算、空值、函数、异常矩阵；tag 与引用槽同时正确复制 |
| String | ASCII/UTF-8/UTF-16、永久 literal、hash、大小写、split/substr/substring/URL、迭代 | `P` | 全 Unicode、非法序列、索引、hash、literal/managed 生命周期与原版差分 |
| typed Array | primitive/object/String/Dynamic、reserve、resize、splice/slice/sort、concat、iterator、raw buffer | `P` | 全 API 与泛型转换矩阵；扩容时精确根、barrier、pin、native raw pointer 安全 |
| `cpp::VirtualArray` | 类型擦除数组和动态数组桥接 | `P` | 所有 typed Array 互转、反射索引、函数参数与返回值 |
| Class | 注册、resolve、super、instance/class fields、ConstructEmpty/Args/Enum、CanCast | `P` | `Type.*` 和 `Reflect.*` 全表；继承静态字段、动态构造、metadata |
| Enum | tag/index/parameters、Resolve、比较、动态索引、反射 | `P` | 无参/有参/递归 enum、参数 GC、Type 构造、比较和字符串化 |
| 闭包 | static/member/local/varargs、绑定接收者、相等性、arity | `P` | 0～5 与 varargs，捕获表达式临时根，成员闭包身份，异常传播 |
| 匿名对象 | 固定字段、动态扩展字段、删除、枚举、toString | `P` | `{}`、结构体 literal、Reflect 增删查改、GC/relocation、并发读取 |
| 动态类字段 | `HX_DECLARE_IMPLEMENT_DYNAMIC` 与每实例 FieldMap | `N/P` | 生成类声明为 dynamic 时可添加任意字段；普通类对合法反射字段读写正确 |
| interface | interface offset/vtable、`_hx_isInstanceOf` | `P` | 多接口、继承接口、cast、native interface、反射 |
| `cpp::Pointer`/Native/Struct/Variant/Int64 | C++ interop 值类型 | `P` | Lime/OpenFL 使用的所有组合；移动 GC 下 derived pointer 必须 pin 或记录 base+offset |
| FieldRef/IndexRef | 动态复合赋值、自增自减 | `N/P` | `obj.x +=`、`obj[i]++`、属性访问模式和异常 |
| Object/Array/String 的 `__GetFields` | Reflect.fields 基础 | `P` | 固定字段、继承字段、动态字段、删除字段不重不漏 |
| Haxe `null` 语义 | nullable primitive、Dynamic null、typed object null | `P` | null 与 0/false/空串严格区分；cast 和比较覆盖 |

### 4.1 生成器必须覆盖的精确 GC 点

原版通过保守栈/寄存器扫描无意中保护了大量 C++ 临时值。新实现不允许退回保守扫描，因此生成器必须显式覆盖：

- 函数参数、局部变量、返回值、异常值和闭包捕获。
- 调用接收者和所有可能在兄弟参数求值时跨 safepoint 的 managed 临时值。
- 匿名对象父对象在字段表达式分配/GC 时的存活。
- 数组 literal、数组赋值、push/insert/扩容期间的 value 与 backing storage。
- 二元运算、比较、字符串拼接、动态 getter/setter 中的两侧临时值。
- 构造函数的 `this`，包括基类构造、字段初始化与异常退出。
- setter 写入 `Dynamic` 时同时复制引用槽、tag 和非引用 payload。
- C++ 求值顺序不确定时，对每个已完成 managed 子表达式先扎根再继续。
- safepoint、异常展开和 native callback 的最终 stack/register map。

当前已加入表达式临时根与 ShaderData 动态 uniform 回归，但这只是此表的一部分，不能由两个 smoke test 推导为“生成器已经完整”。

## 5. 反射、动态字段与当前 `textureScale` 故障

OpenFL 的 `ShaderData` 是：

```haxe
abstract ShaderData(Dynamic) from Dynamic to Dynamic {
    public function new(byteArray:ByteArray) {
        this = {};
    }
}
```

GLSL 解析后会执行：

```haxe
Reflect.setField(__data, name, parameter);
```

`textureScale` 是 Freeplay `BlurFilter` 的合法 uniform。当前用户看到的：

```text
uncaught Haxe/C++ exception:unknown dynamic field: textureScale
```

证明故障发生在 transition/Freeplay 初始化链，不能通过删除 shader 或忽略异常解决。

当前证据：

- 原版 hxcpp 使用 `Anon_obj::mFields`/FieldMap 支持动态添加字段。
- 新 runtime 的 `Anon_obj::Create(0)` 也有动态 FieldState。
- 新增 Haxe 回归已通过：`{} → Full GC → Reflect.setField(textureScale) → Full GC → 读取/覆盖`。
- 2026-07-14 已修正生成器的固定类 `__novaCffiSet`：托管 Object/String/Dynamic 字段必须调用对应 `_hx_set_*`，不得直接 C++ 赋值绕过 owner write barrier；新增“老对象经 `Reflect.setField` 指向新对象/新字符串后执行 young GC”回归并通过。
- 2026-07-14 已增加 XML 精确图回归：连续 64 次 `Xml.parse`，每次执行 young GC 后校验 `nodeName`、属性与子树，当前独立生成器测试通过。整游戏仍必须在引用验证器开启时重新验证，不能以小测试替代。
- 2026-07-14 的原版/新运行时同场景 A/B 揭示 Windows 入口 ABI 缺口：原版导出 `NvOptimusEnablement`/`AmdPowerXpressRequestHighPerformance`，使用 GTX 1050；新 MinGW 可执行文件未导出时落到 Intel HD 630。新 runtime 已自行导出两符号，且经 `dumpbin /exports` 与实际 `GL_RENDERER` 验证恢复独显选择。此项属于原版平台行为契约，不能误归因于 GC。
- 2026-07-14 已确认新 descriptor 回调丢弃 `PropertyAccess`：`Reflect.setProperty(cam, "zoom", value)` 直接写 `zoom`，绕过 `set_zoom()` 对 scale/matrix 的同步；无物理槽的 computed property 也无法访问。新的动态字段 ABI 传递 `paccNever/paccDynamic/paccAlways`，生成器按原版 `AccCall` 规则选择直接槽或 getter/setter，并让 `Reflect.fields` 只枚举数据属性、不混入方法。独立回归覆盖 physical setter、computed getter、字段枚举与原始 `setField` 旁路语义，当前已通过。
- 2026-07-14 已定位 `.00000` 数值显示为 `std::to_string(double)` 的固定六位小数副作用；原版默认 `HX_DOUBLE_PATTERN="%.15g"`。新 String/Dynamic/Array 数值格式已改为等价紧凑格式，并用 `Std.string(7.0)`、字符串拼接及 `[7.0].toString()` 回归验证均为 `"7"`；同时保留反射 Int 的 `Dynamic::Tag::Integer`。
- 因此旧候选包的错误更可能来自表达式临时对象失根、错误对象身份或旧生成代码，而不是 `textureScale` 未声明。

此故障的完成门槛：

1. 新候选包真实进入 Freeplay。
2. Haxe stdout、C++ exception stack、GC stderr 中均无 `textureScale`/`x` 错误。
3. transition tween 正常进出，不闪现、不丢按钮、不只剩可视化与背景。
4. descriptor 诊断能够报告实际接收者类型；未知字段异常不得丢失 Haxe 栈。
5. Full GC、并发 mark、relocation 开启时重复切换仍通过。

## 6. GC 与内存管理功能对照

### 6.1 原版/备份提供的能力

原版 `Immix.cpp`/`GcCommon.cpp` 包含或暴露：

- Immix block/line 分配、hole/recycle、large object 路径、移动/压缩。
- 保守栈与寄存器捕获，以及对象 `__Mark`/`__Visit` 图遍历。
- local allocator、全局 allocator、GC thread pool、并行 mark/move/zero 工作。
- major/minor/update/safepoint、自动触发和内存阈值。
- weak reference、finalizer、zombie、freeze/do-not-kill。
- 对象稳定 ID/hash 和 ID→对象反查。
- GC free zone/native blocking 协议。
- used/reserved/large/process memory 统计、trace/spam collect。
- 备份补丁中的 minor gate/start/base delta、large refresh、parallel/refine thread、max pause、aggressive safepoint、parallel reference processing 配置。

### 6.2 新实现必须用现代机制提供的等价物

| 能力 | 当前状态 | 后续硬目标 |
|---|---|---|
| 精确对象扫描 | `E`（核心测试） | 保持 descriptor/custom trace，无未知内存保活 |
| 精确根 | `P` | 从显式 RootScope 完成到 machine stack/register map；完整表达式根审计 |
| region/TLAB | `E/P` | mixed-live reuse、large/humongous、pinned space、commit/decommit |
| 着色引用/load barrier | `E`（核心测试） | 所有 native/array/string/dynamic 路径审计，减少慢路径 |
| 并发 mark | `P` | 多 worker、work stealing、并发窗口跨帧、短 initial/remark |
| 并发 relocation | `P` | 多 worker、选择移出 STW、失败恢复、短 flip、evacuation reserve |
| SATB/insertion/forwarded store | `E/P` | 覆盖所有生成和 native 写路径，加入 verifier |
| young/minor GC | `P` | 真正 Eden/Survivor/promotion/card/remembered-set，不扫描整个老年代 |
| Weak/Ephemeron/WeakMap | `E`（核心测试） | Haxe 标准库、并发/relocation 压力、keys/values/clear 语义 |
| finalizer/resurrection | `E/P` | 原 API、member/alloc finalizer、zombie queue、backlog budget、shutdown |
| object id/hash | `N` | relocation 后稳定、ID 回查不复活死对象、回收代数防 stale ID |
| freeze/do-not-kill | `P/N` | 定义新精确语义并兼容 Haxe 调用方 |
| GC enable/disable | `A` | 不能是静默 no-op；嵌套、线程与 allocation pressure 测试 |
| GC free/native blocking | `P` | 所有 I/O/CFFI/NDLL 入口审计，raw managed pointer 规则可验证 |
| 配置 API | `N/P` | 保留原调用兼容，同时映射到新 scheduler，而不是返回假值 |
| memory telemetry | `P` | used/reserved/committed/live/native/large/pinned/fragmentation 一致定义 |
| OOM 恢复 | `N` | emergency collect、relocation/promotion/commit failure 有界恢复 |

原版的保守扫描、Immix header、`__Mark`/`__Visit` 不应照搬；它们的可观察正确性必须由 descriptor、精确根、barrier、handle 和 stack map 替代。这些项目标记为 `R`，不是“可以不实现”。

## 7. Thread、同步和 native blocking

| 原版能力 | 当前状态 | 验收 |
|---|---|---|
| Haxe Thread create/current/send/readMessage | `E/P` | 多线程消息对象跨 GC/relocation、线程退出与异常 |
| Mutex | `E/P` | acquire/try/release、异常和 GC blocking |
| Lock | `E/P` | timeout、release 次数、虚假唤醒 |
| TLS | `E/P` | managed value 精确追踪、thread detach 清理 |
| Deque | `E/P` | owner/thief 并发、managed 元素存活 |
| Semaphore | `N` | create/acquire/try/release 与 timeout |
| Condition variable | `N` | acquire/release/wait/timedWait/signal/broadcast |
| thread number/current-thread query | `N/P` | debugger、telemetry 与 native callback 一致 |
| third-party callback attachment | `P` | 未注册线程首次进入自动 attach，退出安全 detach |
| blocking/unblocking/try 协议 | `P` | 任何阻塞路径不拖住 safepoint，返回前重新建立精确根 |

## 8. Classic CFFI、Prime CFFI 与 NDLL

原版 classic CFFI 不只是 `hx_cffi` 能返回几个函数。它包括：

- null/bool/int/float/string/wstring/HxString/array/object/function/abstract/kind 类型系统。
- alloc、value 类型检查、raw/duplicate string、array resize/raw typed pointers。
- buffer create/append/subappend/resize/data/value/string。
- call0～3/callN、object call、traceexcept。
- field ID、字段读写、numeric field、字段迭代。
- finalizer、root、managed-memory accounting、blocking/safepoint。
- dynamic symbol table、NDLL primitive 注册和卸载。
- Prime CFFI 签名、typed thunk、varargs、callback。

当前 classic CFFI 名称盘点为 100 项中命中 67，以下 33 项在当前 CFFI 头/实现中没有同名入口：

```text
alloc_best_int
alloc_field_numeric
alloc_int32
buffer_append_sub
buffer_to_string
create_abstract
free_abstract
gc_change_managed_memory
gc_safe_point
gc_try_blocking
gc_try_unblocking
hx_alloc
hx_error
hx_fail
hx_register_prim
val_array_bool
val_array_float
val_array_int
val_array_set_size
val_array_value
val_buffer
val_call0_traceexcept
val_field_name
val_field_numeric
val_gc_add_root
val_gc_ptr
val_gc_remove_root
val_iter_field_vals
val_iter_fields
val_ocall0
val_ocall1
val_ocall2
val_ocallN
```

这 33 项必须逐一分类为实现、兼容别名或有调用图证据的 `X`。raw array/buffer 指针在移动 GC 下不能直接照搬：返回指针前必须 pin backing object、复制到 native buffer，或让 API 的 scope 明确持有 pinned handle。

## 9. 系统运行时和本地库

| 子系统 | 原版文件 | 当前状态 | 必须补齐 |
|---|---|---|---|
| Std/Sys | `StdLibs.cpp`、`Sys.cpp` | `P` | parse/print/random/field ID/hash/memory/endian/exit/resource 全语义 |
| File | `libs/std/File.cpp` | `P` | open/read/write/seek/tell/eof、错误码、Unicode 路径、blocking/pin |
| Process | `libs/std/Process.cpp` | `P` | pipe/stdin/out/err/exit/kill/close、死锁与 GC |
| Socket | `libs/std/Socket.cpp` | `P` | IPv4/IPv6、DNS、bind/listen/accept/select/timeout/blocking、错误映射 |
| Random | `libs/std/Random.cpp` | `N/P` | 与 Haxe std API、seed 与范围语义一致 |
| Date/timezone | `Date.cpp` | `P` | local/UTC/DST/format/from UTC、边界日期 |
| RegExp | PCRE2 10.42 | `P/R` | 当前 `std::regex` 不能视为等价；恢复 PCRE2 语义或完整差分证明 |
| zlib | zlib 1.3.1 | `P` | deflate/inflate/flush/checksum/error/stream 生命周期 |
| SSL | mbedTLS 2.28.2 | `P/R` | 当前新版本后端需覆盖证书/key/SNI/verify/socket callback/error 语义 |
| SQLite | sqlite 3.40.1 | `N` | NDLL API、row dynamic fields、blob、finalizer、threading |
| MySQL | 自带 protocol/socket/SHA1 | `N` | connection/query/result/dynamic row/error/cleanup |
| embedded resources | `StdLibs.cpp` | `E/P` | 名称、bytes/string、static root 与二进制数据 |
| DLL/NDLL loader | `Lib.cpp` | `P` | 搜索路径、扩展名、静态 primitive、load/unload、错误路径 |
| Neko API bridge | `project/Build.xml` | `N/X` | 若目标不用 Neko，需调用图和构建矩阵证据后标 X |

## 10. 调试、异常、Profiler、Telemetry 与 Tracy

| 能力 | 当前状态 | 目标 |
|---|---|---|
| Haxe Throw/Rethrow | `P` | 任意 Dynamic 异常、C++ 异常边界、finally/destructor、原值保留 |
| Haxe call stack | `N/P` | 文件、类、方法、行号，release 可配置，异常时不为空 |
| exception stack | `N/P` | 捕获第一异常而非后续噪声；跨 callback/thread |
| native stack | `N/P` | Windows symbols/addresses，和 Haxe stack 同一时间戳关联 |
| debugger | `N` | breakpoints、step、thread info、stack variables、scriptable values |
| sampling profiler | `N` | start/stop/dump 与线程安全 |
| HXT telemetry | `N` | frame、allocation、GC overhead、stash/dump/ignore |
| Tracy | `N` | frame mark、zone、message、plot、thread name/group |
| critical error hook | `P` | 不吞异常，handler 重入和失败路径 |

当前游戏诊断要求每个候选包同时记录：Haxe stdout、第一 C++ 异常、Haxe stack、native stack、GC cycle/phase/pause、heap/region/handle、FPS/frame time 和 Windows Event/WER。只记录“进程还活着”不算测试。

## 11. cppia 与 scriptable runtime

原版包含完整 `src/hx/cppia/`：

- cppia bytecode/module/stream/context/variables/functions/classes。
- Array/String/global builtin。
- scriptable class、动态字段、调试变量。
- SLJIT JIT 编译器和 x86/x64/ARM/MIPS/PPC/SPARC/TILEGX 等后端。

当前状态为 `N`。NovaFlare 当前主要使用 HScript/Lua，不代表 cppia 永久不需要。处理规则：

1. 先扫描项目、Lime/OpenFL 和全部 NDLL 是否引用 cppia/scriptable ABI。
2. Windows 游戏主路径若无引用，可从 P0 延后，但不能在“完整 hxcpp 替代”清单中消失。
3. 最终要么重写为兼容解释/JIT 子系统，要么以构建目标分层并给出 `X` 的范围证据。

## 12. 构建系统与平台矩阵

原版 `Build.xml`/build-tool 提供条件编译、静态/动态链接、NDLL、MSVC/Mingw/GCC/Clang、debug/release、平台宏、第三方库和 Neko bridge。当前新 hxcpp 主要是 CMake + 自定义 `HxcppZgcBuild.hx`，Windows/GCC 主路径已经能编译数千个 TU，但不是完整替代。

| 平台/构建能力 | 当前状态 |
|---|---|
| Windows x64 Lime/OpenFL release | `P`，能整包生成/编译/链接，真实状态切换仍失败 |
| Windows debug、console/no_console、资源 `.rc` | `P` |
| MSVC ABI/toolchain | `N/P` |
| MinGW ABI/toolchain | `P` |
| incremental compile/dependency/rsp | `E/P` |
| static link/static primitive | `P/N` |
| NDLL 单独构建与 ABI probe | `P` |
| Linux | `N` |
| Android ARMv7/ARM64 | `N` |
| macOS/iOS/Objective-C bridge | `N` |
| WinRT/UWP | `N` |
| Neko API bridge | `N` |
| third-party PCRE2/SQLite/MySQL/Tracy | `N/P` |

最终必须保留“新 runtime 自己的实现”，但要能消费 Haxe/Lime 产生的项目配置和 native 库。不能要求所有上层库为了新 runtime 改写业务代码。

## 13. 公共 `__hxcpp_*` ABI 缺口清单

下面 138 个名称存在于原版公共头，但在当前 `hxcpp/` 全部 `.h/.hpp/.cpp` 中完全没有出现。它是词法差集，不自动说明都要保持相同二进制布局；每一项都必须被实现、适配或给出 `X` 证据。

### 13.1 GC、finalizer、weak、identity 与配置

```text
__hxcpp_add_alloc_finalizer
__hxcpp_add_member_finalizer
__hxcpp_gc_aggressive_safepoint
__hxcpp_gc_enable_parallel_ref_proc
__hxcpp_gc_get_aggressive_safepoint
__hxcpp_gc_get_large_refresh_enabled
__hxcpp_gc_get_max_pause_ms
__hxcpp_gc_get_parallel_ref_proc_enabled
__hxcpp_gc_get_parallel_threads
__hxcpp_gc_get_refine_threads
__hxcpp_gc_large_refresh_enable
__hxcpp_gc_minor
__hxcpp_gc_reserved_bytes
__hxcpp_gc_set_max_pause_ms
__hxcpp_gc_set_threads
__hxcpp_gc_update
__hxcpp_get_minor_base_delta_bytes
__hxcpp_get_minor_gate_ms
__hxcpp_get_minor_start_bytes
__hxcpp_id_obj
__hxcpp_is_const_string
__hxcpp_obj_hash
__hxcpp_obj_id
__hxcpp_reachable
__hxcpp_set_finalizer
__hxcpp_set_hxt_finalizer
__hxcpp_set_minor_base_delta_bytes
__hxcpp_set_minor_gate_ms
__hxcpp_set_minor_start_bytes
__hxcpp_spam_collects
__hxcpp_weak_ref_create
__hxcpp_weak_ref_get
```

### 13.2 Thread、condition 与 semaphore

```text
__hxcpp_condition_acquire
__hxcpp_condition_broadcast
__hxcpp_condition_create
__hxcpp_condition_release
__hxcpp_condition_signal
__hxcpp_condition_timed_wait
__hxcpp_condition_try_acquire
__hxcpp_condition_wait
__hxcpp_GetCurrentThreadNumber
__hxcpp_is_current_thread
__hxcpp_semaphore_acquire
__hxcpp_semaphore_create
__hxcpp_semaphore_release
__hxcpp_semaphore_try_acquire
```

### 13.3 调试、调用栈、profiler、HXT 与 Tracy

```text
__hxcpp_dbg_addClassFunctionBreakpoint
__hxcpp_dbg_addFileLineBreakpoint
__hxcpp_dbg_breakNow
__hxcpp_dbg_checkedRethrow
__hxcpp_dbg_checkedThrow
__hxcpp_dbg_continueThreads
__hxcpp_dbg_deleteAllBreakpoints
__hxcpp_dbg_deleteBreakpoint
__hxcpp_dbg_enableCurrentThreadDebugging
__hxcpp_dbg_fix_critical_error
__hxcpp_dbg_getClasses
__hxcpp_dbg_getCurrentThreadNumber
__hxcpp_dbg_getFiles
__hxcpp_dbg_getFilesFullPath
__hxcpp_dbg_getScriptableClasses
__hxcpp_dbg_getScriptableFiles
__hxcpp_dbg_getScriptableFilesFullPath
__hxcpp_dbg_getScriptableValue
__hxcpp_dbg_getScriptableVariables
__hxcpp_dbg_getStackVariables
__hxcpp_dbg_getStackVariableValue
__hxcpp_dbg_getThreadInfo
__hxcpp_dbg_getThreadInfos
__hxcpp_dbg_setAddParameterToStackFrameFunction
__hxcpp_dbg_setAddStackFrameToThreadInfoFunction
__hxcpp_dbg_setEventNotificationHandler
__hxcpp_dbg_setNewParameterFunction
__hxcpp_dbg_setNewStackFrameFunction
__hxcpp_dbg_setNewThreadInfoFunction
__hxcpp_dbg_setOnScriptLoadedFunction
__hxcpp_dbg_setScriptableValue
__hxcpp_dbg_setStackVariableValue
__hxcpp_dbg_stepThread
__hxcpp_dbg_threadCreatedOrTerminated
__hxcpp_execution_trace
__hxcpp_get_call_stack
__hxcpp_get_exception_stack
__hxcpp_hxt_dump_telemetry
__hxcpp_hxt_ignore_allocs
__hxcpp_hxt_start_telemetry
__hxcpp_hxt_stash_telemetry
__hxcpp_on_line_changed
__hxcpp_set_debugger_info
__hxcpp_set_stack_frame_line
__hxcpp_stack_begin_catch
__hxcpp_start_profiler
__hxcpp_stop_profiler
__hxcpp_tracy_framemark
__hxcpp_tracy_get_zone_count
__hxcpp_tracy_message
__hxcpp_tracy_message_app_info
__hxcpp_tracy_plot
__hxcpp_tracy_plot_config
__hxcpp_tracy_set_thread_name_and_group
```

### 13.4 标准运行时、反射、数值、内存、日期和加载器

```text
__hxcpp_align_get_float32
__hxcpp_align_get_float64
__hxcpp_align_set_float32
__hxcpp_align_set_float64
__hxcpp_boot_std_classes
__hxcpp_cast_get_proc_address
__hxcpp_check_overflow
__hxcpp_drand
__hxcpp_enum_force
__hxcpp_field_from_id
__hxcpp_field_to_id
__hxcpp_from_utc
__hxcpp_get_class_list
__hxcpp_get_kind
__hxcpp_get_proc_address
__hxcpp_irand
__hxcpp_is_dst
__hxcpp_main
__hxcpp_memory
__hxcpp_memory_clear
__hxcpp_memory_get_f32
__hxcpp_memory_get_i32
__hxcpp_memory_select
__hxcpp_memory_set_f32
__hxcpp_parse_float
__hxcpp_parse_int
__hxcpp_parse_substr_float
__hxcpp_parse_substr_int
__hxcpp_print_string
__hxcpp_println_string
__hxcpp_register_prim
__hxcpp_reverse_endian
__hxcpp_run_dll
__hxcpp_stdlibs_boot
__hxcpp_time_stamp
__hxcpp_to_utc_string
__hxcpp_unsafe_set
__hxcpp_utf8_string_to_char_array
```

### 13.5 说明

原差集还包含 `__hxcpp_lib_main`，当前源码中已有该名字，因扫描时声明位置差异已归入“命中但待验证”，不重复计入以上分组。每次更新 runtime 后必须重新执行符号差集，文档中的数字也要同步更新。

## 14. 当前已经存在但仍需逐项验证的原名入口

当前命中的 91 个原名主要覆盖：collect/compact、部分 GC stats、resources、日期、内存读写、Mutex/Lock/TLS/Deque/Thread、WeakMap 相关基础、Array/String 转换、基础 DLL 路径和打印。它们的状态统一暂定为 `A` 或矩阵中的 `P/E`，不得因为同名就判定完成。

验证维度至少包含：

- C/C++ 签名和调用约定。
- Haxe 可观察返回值、null、异常类型和异常文本。
- managed 参数在调用前、调用中和返回后的精确存活。
- relocation 后地址、identity 和 field/index 行为。
- native blocking、callback thread attach 和 shutdown。
- 多线程竞争、重入、finalizer 与异常同时发生。

## 15. 实现顺序

### P0：真实游戏主路径与第一异常

1. 完成当前修复后的整包构建。
2. 审计生成的 MainMenu tween/匿名对象，确认没有裸 managed 表达式临时值。
3. 启动并记录 Haxe、C++、GC、Windows、画面和帧时间。
4. 实际操作 Title → MainMenu → Freeplay。
5. 修复第一处 `x`/`textureScale`/空引用，不让日志风暴掩盖根异常。
6. 把每个真实故障压缩成独立 Haxe/runtime 回归测试。
7. 对照备份验证 Windows GPU 选择导出、`PropertyAccess` getter/setter 副作用、computed property 与 `Reflect.fields`；Title/MainMenu FPS 差异必须在同一 `GL_RENDERER` 下比较。
8. 验证 Int/Float tag 与原版紧凑数字字符串，不允许本应显示整数的值被格式化为 `.000000`。

### P1：Haxe 语言和对象语义

1. Dynamic 全转换/运算/call/tag。
2. Object/Class/Enum/interface/closure/anonymous/dynamic class。
3. Reflect/Type/RTTI/metadata/field/index/property access。
4. String/Array/VirtualArray/Pointer/Variant/Int64。
5. 生成器表达式根、barrier、异常和构造器审计。

### P2：CFFI、NDLL 与 native 生命周期

1. 补齐 33 个 classic CFFI 缺口。
2. Prime/classic 全 arity 和 callback。
3. raw pointer pin/handle/native-buffer 规则。
4. Lime/OpenFL 实际加载的每个 primitive 建立 ABI probe。
5. 音频、GL、输入、Discord、Lua、VLC 等项目 native 库逐个回归。

### P3：Thread、异常栈与诊断

1. Condition/Semaphore/thread identity。
2. callback attachment 与 blocking 协议。
3. Haxe call/exception stack 和 Windows native stack。
4. 第一异常捕获、GC phase/frame telemetry。

### P4：原 GC API 语义与真正低停顿内核

1. 原 32 个 GC/weak/finalizer/identity/config 缺口。
2. 真正 young generation、card table、remembered-set、promotion。
3. 多 worker mark/relocation/ref processing/work stealing。
4. 把 heap-size 相关工作移出 initial/remark/flip 暂停。
5. pinned/large/humongous/mixed-live/commit/decommit。
6. OOM 与所有失败恢复。

### P5：本地库、工具和跨平台

1. PCRE2、zlib、SSL、SQLite、MySQL 完整语义。
2. profiler/HXT/Tracy/debugger。
3. cppia/scriptable。
4. MSVC/Linux/Android/macOS/iOS/WinRT/Neko/static link。

## 16. 每一项的标准验收模板

每个清单项完成时必须同时提交：

1. 原版行为证据：源文件、公开声明、最小 Haxe/C++ 调用例。
2. 新实现说明：数据结构、线程协议、barrier/root/handle 规则。
3. 差分测试：同一输入在原版与新 runtime 的值、异常和副作用。
4. GC 压力：调用前后强制 Full GC、young GC、concurrent mark、relocation。
5. native 压力：blocking、callback、跨线程、shutdown。
6. 失败测试：null、越界、非法类型、OOM/分配失败、重复释放。
7. 集成验证：至少一个 Lime/OpenFL 或 NovaFlare 的真实调用路径。
8. 性能数据：热路径吞吐、分配量、barrier/load-barrier 命中、P99/P99.9/max。

## 17. 构建和游戏最终门槛

在以下全部通过前，不能宣称“比原版更好”“无卡顿”或“完整替代”：

- 公共 ABI/功能清单无未分类项目。
- 核心 unit/smoke/ABI/differential 测试全部通过。
- `lime test windows` 完整生成、编译、链接、启动。
- Title、MainMenu、Freeplay、设置、选歌、加载、PlayState、暂停、恢复、退出、重进正常。
- transition tween、按钮、shader、音频可视化和背景画面正确。
- Haxe 日志无未捕获异常；native/GC verifier/Windows Event 无错误。
- 不存在持续低 FPS、日志风暴、内存无界增长、handle/finalizer/weak 表膨胀。
- GC initial mark、remark、relocation flip 有严格预算；常规阶段不做 heap-size 线性 STW 工作。
- 单轮最终稳定性测试最长 20 分钟，期间持续记录 frame time 与 GC/heap 关联。
- 与原版 hxcpp 在相同场景、相同设置、相同资源下进行性能基准；至少 P99/P99.9/max 和内存不能退化。

## 18. 当前检查点

截至本文建立时：

- 新 GC 核心测试 19/19 已通过。
- Haxe 生成 smoke 已通过，包括强制 Full GC 的调用临时根与 ShaderData `textureScale` 动态字段回归。
- 完整 NovaFlare 项目正在使用修复后的生成器重新编译，共 2761 个 translation units，2507 个需要编译。
- 旧候选包仍被判定为失败：主菜单确认后 transition 异常、`x` 日志风暴、Freeplay 初始化 `unknown dynamic field: textureScale`、FPS 崩溃。
- 失败判定不会因独立 smoke 通过而撤销；只有新整包真实操作路径通过才可关闭。

本文以后作为主清单更新，不再用零散口头结论替代原版功能对照。

## 19. 原版 54 个运行时源文件的逐文件账目

这一节禁止用 `*` 或“同类文件”省略。状态是本文件所承担语义在新 runtime 中的当前状态，不是“存在同名文件”状态。每一行只有按第 16 节交齐证据后才能改为 `E`。

| # | 原版文件 | 不可遗漏的责任 | 状态/新实现目标 |
|---:|---|---|---|
| 1 | `src/Array.cpp` | typed/virtual Array 运行时、反射、转换和通用操作 | `P`：补齐全 API、扩容根与 barrier 差分 |
| 2 | `src/Dynamic.cpp` | Dynamic boxing、转换、运算、调用和异常 | `P/R`：完成数值/对象/函数/空值矩阵 |
| 3 | `src/Enum.cpp` | enum tag、参数、比较、反射和字符串化 | `P`：补递归 enum 与 GC 压力 |
| 4 | `src/ExampleMain.cpp` | 嵌入式/示例入口契约 | `N`：判断是否纳入发行；否则以证据标 `X` |
| 5 | `src/Math.cpp` | Math facade 与随机数入口 | `P`：逐函数差分 |
| 6 | `src/ObjcData.mm` | Objective-C 数据和动态桥接 | `N`：Apple 阶段实现，Windows 暂不得冒充完成 |
| 7 | `src/String.cpp` | String 分配、编码、hash、搜索、转换和操作 | `P`：Unicode/非法序列/literal 生命周期矩阵 |
| 8 | `src/hx/AndroidCompat.cpp` | Android libc/platform compatibility | `N`：Android 平台阶段实现 |
| 9 | `src/hx/Anon.cpp` | 匿名对象固定/扩展字段、删除、枚举、toString | `P`：GC/relocation/并发字段压力 |
| 10 | `src/hx/Boot.cpp` | runtime boot、平台初始化和异常边界 | `P`：启动/关闭/重复初始化差分 |
| 11 | `src/hx/CFFI.cpp` | classic CFFI 全表、root、buffer、abstract、call、blocking | `P`：补齐 33 个缺口并做 NDLL 生命周期测试 |
| 12 | `src/hx/Class.cpp` | class registry、RTTI、construct、cast 和字段枚举 | `P`：Type/Reflect 全矩阵 |
| 13 | `src/hx/Date.cpp` | local/UTC/date/timezone | `P`：时区、DST、边界时间差分 |
| 14 | `src/hx/Debug.cpp` | 调用栈 frame、异常栈、文件行号和变量元数据 | `N/P`：恢复非空 stack-frame 宏和可观测 Haxe 栈 |
| 15 | `src/hx/Debugger.cpp` | breakpoint、step、线程、stack-variable debugger | `N`：独立调试协议子系统 |
| 16 | `src/hx/Hash.cpp` | int/int64/string/object hash backing maps | `N/P`：按键类型、弱键、identity 与 GC 差分 |
| 17 | `src/hx/Lib.cpp` | NDLL/DLL 搜索、加载、符号和 primitive registry | `P`：错误路径、卸载、并发 callback |
| 18 | `src/hx/NoFiles.cpp` | 无调试文件表构建模式 | `N`：构建矩阵覆盖 |
| 19 | `src/hx/Object.cpp` | Object 虚协议、字段、索引、比较、class、调用默认行为 | `P/R`：完整 ABI 和异常接收者诊断 |
| 20 | `src/hx/Profiler.cpp` | sampling profiler | `N`：线程安全采样及低开销关闭态 |
| 21 | `src/hx/RunLibs.cpp` | 库模式 main、静态库注册和启动 | `N/P`：可执行/库/static-link 三种入口测试 |
| 22 | `src/hx/StdLibs.cpp` | resources、print、parse、hash、field id、memory、random | `P`：逐 primitive ABI/行为差分 |
| 23 | `src/hx/Telemetry.cpp` | HXT frame、allocation、GC telemetry | `N`：事件协议、缓冲和关闭态开销 |
| 24 | `src/hx/TelemetryTracy.cpp` | Tracy zone、message、plot、thread 集成 | `N`：编译开关及 GC phase 事件 |
| 25 | `src/hx/Thread.cpp` | Thread、Mutex、Lock、TLS、Deque、Condition、Semaphore | `P`：竞争、超时、关闭及 GC handshake 矩阵 |
| 26 | `src/hx/gc/GcCommon.cpp` | GC facade、配置、enable/disable/freeze、统计 | `P/R`：新内核等价协议和公开入口 |
| 27 | `src/hx/gc/GcRegCapture.cpp` | 原版保守寄存器根捕获 | `R/P`：用精确 stack/register map 取代并验证 native 边界 |
| 28 | `src/hx/gc/Immix.cpp` | 分配、major/minor、mark/move、weak/finalizer、线程池 | `R/P`：ZGC 风格并发内核，不复制 Immix 算法但语义不缺项 |
| 29 | `src/hx/cppia/ArrayBuiltin.cpp` | cppia Array builtin 指令和快速路径 | `N`：scriptable/cppia 阶段 |
| 30 | `src/hx/cppia/ArrayVirtual.cpp` | cppia typed/virtual Array 桥接 | `N`：解释器/JIT 与 runtime Array 一致 |
| 31 | `src/hx/cppia/Cppia.cpp` | cppia 公共入口、核心值/操作码运行时 | `N`：版本、加载、执行和异常协议 |
| 32 | `src/hx/cppia/CppiaClasses.cpp` | cppia class/interface/enum/scriptable 对象 | `N`：动态类注册、继承、GC descriptor |
| 33 | `src/hx/cppia/CppiaCompiler.cpp` | cppia JIT/compiler 和机器码后端连接 | `N`：架构后端、W^X、stack map |
| 34 | `src/hx/cppia/CppiaCtx.cpp` | 解释执行 context、栈和异常展开 | `N`：精确解释器 frame roots |
| 35 | `src/hx/cppia/CppiaFunction.cpp` | cppia function、closure、参数和调用约定 | `N`：arity/varargs/capture/exception |
| 36 | `src/hx/cppia/CppiaModule.cpp` | 模块解析、类型/资源/函数表和链接 | `N`：binary compatibility 与失败诊断 |
| 37 | `src/hx/cppia/CppiaVars.cpp` | local/member/static/index lvalue 与变量访问 | `N`：写屏障和复合赋值 |
| 38 | `src/hx/cppia/GlobalBuiltin.cpp` | 全局 builtin/标准函数 dispatch | `N`：逐 builtin 差分 |
| 39 | `src/hx/cppia/HaxeNative.cpp` | Haxe/native 函数和类型桥接 | `N`：handle、pin、callback、blocking |
| 40 | `src/hx/cppia/StringBuiltin.cpp` | cppia String builtin 指令和快速路径 | `N`：与 managed String 语义一致 |
| 41 | `src/hx/libs/mysql/Mysql.cpp` | MySQL NDLL 对外连接/查询/result API | `N`：完整客户端语义 |
| 42 | `src/hx/libs/mysql/my_api.cpp` | MySQL protocol API 和结果转换 | `N`：协议/错误/GC roots |
| 43 | `src/hx/libs/mysql/my_proto.cpp` | MySQL wire protocol 编解码和认证流程 | `N`：边界包和失败路径 |
| 44 | `src/hx/libs/mysql/sha1.cpp` | 旧认证所需 SHA-1 辅助 | `N`：保持协议兼容并隔离用途 |
| 45 | `src/hx/libs/mysql/socket.cpp` | MySQL socket/传输层 | `N`：blocking protocol、超时、关闭 |
| 46 | `src/hx/libs/regexp/RegExp.cpp` | PCRE regexp primitive | `P/R`：行为/错误/Unicode 与原版差分 |
| 47 | `src/hx/libs/sqlite/Sqlite.cpp` | SQLite NDLL、statement/result/value bridge | `N`：native handle/finalizer/thread 测试 |
| 48 | `src/hx/libs/ssl/SSL.cpp` | TLS、证书、密钥、socket glue | `P/R`：目标 TLS 后端的等价公开语义 |
| 49 | `src/hx/libs/std/File.cpp` | 文件和 pipe I/O primitives | `P`：二进制、EOF、错误码、Unicode path |
| 50 | `src/hx/libs/std/Process.cpp` | 子进程、stdin/out/err、exit/kill | `P`：死锁、关闭、并发和 Windows quoting |
| 51 | `src/hx/libs/std/Random.cpp` | std random primitive | `N/P`：seed、范围、线程与分布接口差分 |
| 52 | `src/hx/libs/std/Socket.cpp` | socket、DNS、select、blocking | `P`：IPv4/6、错误/超时及 GC blocking |
| 53 | `src/hx/libs/std/Sys.cpp` | filesystem、env、path、time、system | `P`：Windows/Unicode/异常矩阵 |
| 54 | `src/hx/libs/zlib/ZLib.cpp` | zlib deflate/inflate streaming primitive | `P`：流状态、flush、错误和 finalizer |

完整性校验规则：`rg --files D:\game\superbackup\git\src` 过滤 `.cpp/.mm` 必须仍为 54；任何新增、删除或基线变化都要更新本表和 commit ID。

## 20. 原版 56 个公共头文件的逐文件 ABI 账目

这些头文件代表生成代码、手写 C++、NDLL 或平台层可能直接依赖的契约。即使内部采用完全不同的数据结构，也不能在没有迁移/替代协议和测试的情况下静默丢弃。

| # | 原版头文件 | 契约 | 状态 |
|---:|---|---|---|
| 1 | `include/Array.h` | typed Array 模板和公开 ABI | `P/R` |
| 2 | `include/Dynamic.h` | Dynamic 表示、转换、调用与运算 | `P/R` |
| 3 | `include/Enum.h` | Enum 对象与参数协议 | `P` |
| 4 | `include/hxMath.h` | Math facade | `P` |
| 5 | `include/hxString.h` | String ABI 和 API | `P/R` |
| 6 | `include/hxcpp.h` | 总入口、生成宏、基础类型和版本契约 | `P/R` |
| 7 | `include/null.h` | Haxe null/nullable 语义 | `P` |
| 8 | `include/cpp/CppInt32__.h` | cpp.Int32 抽象桥接 | `P` |
| 9 | `include/cpp/FastIterator.h` | fast iterator 模板 | `P` |
| 10 | `include/cpp/Int64.h` | Int64 表示与运算 | `P` |
| 11 | `include/cpp/Pointer.h` | native pointer、raw/auto cast 和算术 | `P/R` |
| 12 | `include/cpp/Variant.h` | C++ variant interop | `P` |
| 13 | `include/cpp/VirtualArray.h` | 类型擦除数组桥接 | `P` |
| 14 | `include/hx/Anon.h` | 匿名对象 ABI | `P` |
| 15 | `include/hx/Boot.h` | boot 和入口声明 | `P` |
| 16 | `include/hx/CFFI.h` | classic CFFI 主 API | `P` |
| 17 | `include/hx/CFFIAPI.h` | CFFI 动态 API 表/加载契约 | `P/N` |
| 18 | `include/hx/CFFIJsPrime.h` | JavaScript Prime bridge | `N` |
| 19 | `include/hx/CFFILoader.h` | CFFI loader 宏和 primitive 绑定 | `P/N` |
| 20 | `include/hx/CFFINekoLoader.h` | Neko CFFI loader | `N` |
| 21 | `include/hx/CFFIPrime.h` | Prime CFFI 类型签名和包装 | `P/N` |
| 22 | `include/hx/Class.h` | Class/RTTI/registry ABI | `P` |
| 23 | `include/hx/Debug.h` | stack frame、异常栈和调试元数据 | `N/P` |
| 24 | `include/hx/DynamicImpl.h` | Dynamic 模板实现细节和 operator | `P/R` |
| 25 | `include/hx/ErrorCodes.h` | 平台错误码映射 | `N/P` |
| 26 | `include/hx/FieldRef.h` | 动态字段引用和复合赋值 | `N/P` |
| 27 | `include/hx/Functions.h` | closure/function 0～N ABI | `P/R` |
| 28 | `include/hx/GC.h` | GC allocation/root/finalizer/weak 公开协议 | `P/R` |
| 29 | `include/hx/GcTypeInference.h` | 生成类型的 GC layout 推断 | `P/R` |
| 30 | `include/hx/HeaderVersion.h` | header/binary ABI 版本检查 | `P/N` |
| 31 | `include/hx/HxcppMain.h` | 可执行/库入口宏 | `P/N` |
| 32 | `include/hx/IndexRef.h` | 动态索引引用和复合赋值 | `N/P` |
| 33 | `include/hx/Interface.h` | interface offset/vtable/cast | `P/R` |
| 34 | `include/hx/LessThanEq.h` | 泛型排序比较辅助 | `P` |
| 35 | `include/hx/Macros.h` | 普通生成代码宏 ABI | `P/R` |
| 36 | `include/hx/MacrosFixed.h` | fixed/jumbo 生成模式宏 | `N/P` |
| 37 | `include/hx/MacrosJumbo.h` | jumbo 生成模式宏 | `N/P` |
| 38 | `include/hx/Memory.h` | native/managed memory helpers | `P/R` |
| 39 | `include/hx/Native.h` | native class/function bridge | `N/P` |
| 40 | `include/hx/NekoFunc.h` | Neko function bridge | `N` |
| 41 | `include/hx/ObjcHelpers.h` | Objective-C bridge helpers | `N` |
| 42 | `include/hx/Object.h` | Object 虚协议和 ObjectPtr | `P/R` |
| 43 | `include/hx/Operators.h` | Dynamic/Haxe 运算规则 | `P/R` |
| 44 | `include/hx/OS.h` | 平台检测、同步和导出宏 | `P` |
| 45 | `include/hx/QuickVec.h` | runtime 内部 vector/container 辅助 | `N/P` |
| 46 | `include/hx/Scriptable.h` | scriptable class/cppia bridge | `N` |
| 47 | `include/hx/StackContext.h` | 精确/保守栈上下文协议 | `P/R` |
| 48 | `include/hx/StdLibs.h` | std primitive 声明 | `P` |
| 49 | `include/hx/StdString.h` | std::string/String 转换 | `P` |
| 50 | `include/hx/StringAlloc.h` | String 分配、literal 和编码辅助 | `P/R` |
| 51 | `include/hx/Telemetry.h` | HXT telemetry API | `N` |
| 52 | `include/hx/TelemetryTracy.h` | Tracy telemetry API/宏 | `N` |
| 53 | `include/hx/Thread.h` | Thread/synchronization API | `P` |
| 54 | `include/hx/Tls.h` | TLS wrapper/slot 生命周期 | `P` |
| 55 | `include/hx/Undefine.h` | 宏污染清理契约 | `N/P` |
| 56 | `include/hx/Unordered.h` | unordered 容器兼容层 | `N/P` |

完整性校验规则：`rg --files D:\game\superbackup\git\include` 过滤 `.h/.hpp` 必须仍为 56。实现头文件数量无需机械等于 56，但每行的生成 ABI、源码 ABI 或明确替代协议都必须有自动测试。

## 21. 清单关闭纪律

1. `P`、`N`、`A`、`R` 都表示未完成，不可用于“已经兼容”的结论。
2. 同名 symbol、同名类或能链接，只能证明名称存在；必须继续验证布局、调用约定、返回值、异常、线程和 GC 生命周期。
3. Windows/NovaFlare 的 P0 故障优先修复，但不能因此删掉 cppia、Apple、Android、Neko 等账目；暂不适用项只能带证据改为 `X`。
4. 每完成一项，同步记录测试命令、结果文件、基线 commit 和性能数据，避免再次靠人工记忆重复排查。
5. 真实应用第一次异常是主因；后续 `x` 日志风暴、低 FPS、画面停滞只能作为放大结果，不能掩盖最先发生的对象/根/ABI 错误。

## 22. 2026-07-15：Class 静态反射与 Freeplay `songPosiData`

真实游戏已经完成 Title -> MainMenu -> Freeplay 的状态创建。Freeplay 首次稳定暴露的 Haxe 异常为 `unknown dynamic field: songPosiData`；源码路径是 `MouseMove` 将 `Class<FreeplayState>` 作为反射目标，并对 `FreeplayState.songPosiData` 调用 `Reflect.getProperty/setProperty`。原版教师实现位于 `D:\game\superbackup\git\src\hx\Class.cpp` 与生成器的 `__GetStatic/__SetStatic`。

本轮新架构实现（不调用 LegacyBridge）：

- `TypeDescriptor` 新增静态字段 get/set 回调、静态字段名表、实例成员名表；
- `Class_obj` 把 Class 对象上的动态字段访问转发给所表示类型的静态回调；
- 生成器为每个类发出 `__novaCffiGetStatic/__novaCffiSetStatic`；
- `Reflect.field/setField` 保持 raw 语义，`Reflect.getProperty/setProperty` 对 `AccCall` 调 getter/setter；
- 无物理存储的计算型静态属性可由 `getProperty` 调 getter，但与原版一致，不伪装成 `Type.getClassFields` 中的存储字段；
- `Type.getClassFields` 返回本类静态数据/方法，`Type.getInstanceFields` 递归合并父类成员并去重；
- 新生成回归覆盖静态 raw/property、计算属性、静态方法、继承数据/方法和去重。

已完成证据：

- `tools/build-hxcpp-zgc-kernel.ps1`：19/19 通过；
- `tools/test-hxcpp-zgc-haxe-smoke.ps1`：`Haxe-generated hxcpp ZGC smoke test passed`；
- 生成的 `StaticPropertyTarget_obj::__novaCffiGetStatic` 已包含计算 getter；
- 下一门禁：完整重新生成 NovaFlare，确认 `FreeplayState_obj` 描述符包含 `songPosiData`，再进入 Freeplay检查 Haxe/原生栈/GC 日志。
