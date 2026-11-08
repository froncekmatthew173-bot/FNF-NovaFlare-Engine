# NovaFlare 新 hxcpp：当前进度、剩余工作与恢复点

> 状态日期：2026-07-13  
> 实现目标：从零实现一个对外仍名为 `hxcpp`、但不依赖旧 hxcpp GC、Immix 或 LegacyBridge 的现代 Haxe C++ 运行时；采用精确追踪、并发标记、并发移动、彩色引用和低停顿调度，最终以真实 NovaFlare 游戏路径验证。  
> Windows 最终候选版本的单轮稳定性测试上限：20 分钟。  
> 当前阶段：完整项目已能生成、编译和链接；正在打通 Lime Prime/CFFI 原生调用边界，程序尚未稳定启动到窗口和主菜单。

## 1. 一页结论

### 1.1 现在做到哪里

- 新 GC 和运行时的核心原型已经成立，不是旧 GC 的包装层。
- 自定义 Haxe C++ 后端可以生成完整 NovaFlare 项目。
- 2761 个 C++ 翻译单元已经完成过完整构建；最近一次受生成头变化影响的 1139 个单元也全部编译并成功链接。
- 最新已链接的 `ApplicationMain.exe` 为 791,668,875 字节，时间戳为 2026-07-13 18:55:03。
- 已修复此前 `FlxPool.get()` 中 Dynamic setter 丢失 tag/载荷的问题；生成审计为 743/743 正确、0 遗漏。
- 最新程序已经越过 `FlxPool` 故障，当前第一异常前移到 `ImageDataUtil.fillRect -> NativeCFFI`。
- Prime/CFFI 当前故障的根因已经确认，不再只是猜测；修复涉及 Haxe 生成类型、Prime ABI 编组和 NDLL loader 三层。
- 2026-07-13 本次现场复测：核心 CTest 16/16 通过，总耗时 0.38 秒。
- no-legacy 审计通过：新堆没有重新依赖旧 GC、Immix 或 LegacyBridge。

### 1.2 还差多少

按最终目标的加权工程工作量粗估：

- **已完成约 30%～40%。**
- **剩余约 60%～70%。**

这个比例不是时间承诺。它反映的是“可发布、低停顿、能稳定运行完整游戏”的总工程量，而不是代码行数。当前核心原型完成度已经明显高于应用集成完成度，但以下大项仍未完成：

- 完整 Lime/OpenFL/第三方 NDLL 兼容。
- 稳定启动、菜单、设置、谱面和切歌等真实功能路径。
- 机器级 stack map/register map。
- 真正的 nursery/Survivor/old 分代收集和 remembered set。
- 多 GC worker、work stealing 和自适应调度。
- 严格有界的 initial-mark/remark/relocation-flip 暂停。
- 完整 OOM、promotion failure、relocation failure 恢复。
- 帧时间/GC 阶段联合遥测和自动性能门禁。
- `lime test windows` 最终闭环及最长 20 分钟稳定性验收。
- Windows 之后的 Linux、Android、macOS、iOS 适配。

因此目前不能宣称“新 hxcpp 已完成”“已经比正式 hxcpp 更快”或“已经无卡顿”。

## 2. 已经实现并验证的内容

以下内容中的“完成”表示当前设计范围内已有实现和回归测试，不等于已经达到最终生产级性能上限。

### 2.1 独立运行时与单一新堆

- 独立 `hxcpp/` haxelib、构建入口和 Windows 工具链。
- 新分配器、对象模型和 GC 生命周期，不调用旧 hxcpp 收集器。
- no-legacy 静态审计脚本。
- 多阶段备份和已知可链接恢复点。

### 2.2 精确对象与引用模型

- 4 字节对象头基础 ABI。
- 彩色引用和引用解码。
- 精确 `TypeDescriptor`、继承描述和自定义 trace 入口。
- 精确 `RootFrame`、`RootScope`、静态根和 handle 根。
- 不依赖保守扫描来发现普通托管对象。
- owner-aware store barrier 和 load barrier。

当前精确性依靠显式根帧和生成的字段描述符；机器级 stack/register map 尚未完成。

### 2.3 堆、分配与移动

- Region 基础结构。
- TLAB 快速分配路径。
- object-start 元数据。
- 并发标记基础版。
- 并发重定位基础版。
- forwarding/self-healing load barrier。
- late pin 和 pinned handle。
- strong、weak、pinned handle。

当前版本仍只有基础 worker 模型，也没有完整分代 nursery。

### 2.4 引用处理

- Weak reference。
- Ephemeron。
- WeakMap。
- Finalizer。
- Resurrection 基础语义。

尚缺 soft/phantom/zombie 引用、最终化积压预算和完整故障注入。

### 2.5 Haxe 值模型和代码生成

- Object、String、Dynamic、Array 的基础运行时模型。
- Class、Enum、Closure、异常、反射所需的部分入口。
- 新 Haxe C++ 生成器能够生成完整 NovaFlare 项目。
- 生成器能够输出精确字段描述符、根注册和写屏障。
- Dynamic setter 会同时复制引用和非引用元数据。

真实程序仍会继续暴露未覆盖的 Haxe 边界语义，因此这些值模型目前属于“主干可用、兼容未封口”。

### 2.6 CFFI/NDLL 基础层

- 稳定 CFFI `ValueToken`，不把可移动堆地址直接暴露给 NDLL。
- Dynamic 与 CFFI value 的基础双向转换。
- CallScope 生命周期和字符串临时 pin。
- classic CFFI closure 基础路径。
- 内部注册 Prime primitive 的基础路径。
- Windows `LoadLibrary/GetProcAddress` 基础加载。

当前未完成的是“外部 Lime NDLL 的 typed Prime 调用桥”，这正是最新启动阻塞点。

## 3. 完整 NovaFlare 集成已经到达的边界

### 3.1 最近一次成功构建

| 项目 | 结果 |
|---|---:|
| 完整生成 | 成功 |
| C++ 翻译单元总数 | 2761 |
| 最近一轮实际重编译 | 1139 |
| 重编译完成 | 1139/1139 |
| 最终链接 | 成功 |
| 输出 | `export/release/windows/obj/ApplicationMain.exe` |
| 大小 | 791,668,875 字节 |
| 时间戳 | 2026-07-13 18:55:03 |

该 exe 对应的是当前 Prime ABI 修正之前的已链接版本。生成器和 Lime 源码已有更新，但还没有重新生成、编译并形成下一版 exe。

### 3.2 已越过的真实程序故障

最近已经依次处理或越过：

- 基础 Haxe frontend/生成 ABI。
- TLS 和完整链接问题。
- interface/dynamic 转换。
- class/reflect 路径。
- null 语义。
- Array iterator。
- Array 转换。
- `FlxPool._constructor` 的 Dynamic setter 元数据丢失。

这些修复证明新运行时已经能承载越来越深的真实启动路径，但还不等于整条启动链已稳定。

### 3.3 当前第一故障

最新已知异常：

```text
uncaught Haxe/C++ exception: Dynamic value is not callable
```

第一异常调用路径：

```text
lime._internal.graphics.ImageDataUtil.fillRect
  -> lime._internal.backend.native.NativeCFFI
  -> lime_image_data_util_fill_rect
```

旧生成调用点：

```text
export/release/windows/obj/src/lime/_internal/graphics/ImageDataUtil.cpp:2348
```

## 4. Prime/CFFI 根因：已确认

### 4.1 实际发生了什么

Lime 导出的 `lime_image_data_util_fill_rect__prime` 是一个 Prime 工厂。它返回的不是可直接跳转执行的 C++ 函数地址，而是通过 `cffi::alloc_pointer` 包装出来的 CFFI `value` token。

当前新运行时的 `__hxcpp_zgc_load_prime` 做了以下错误操作：

1. 找到 `*_prime` 工厂。
2. 调用工厂并取得返回值。
3. 把返回的 CFFI token 直接当成原生函数地址。
4. 将它包装成 `Dynamic::NativePointer`。

此外，之前为了临时绕过 typed Callable，Lime 的部分 hxcpp_zgc 生成路径曾把 Prime 字段改成 `Dynamic`。但 `Dynamic::invoke` 只会调用托管 closure 对象，不会调用 `NativePointer`，于是出现 `Dynamic value is not callable`。

### 4.2 正确修复方向

正确边界应当是：

```text
Haxe typed cpp.Callable/cpp.Function
  -> ExternalPrimeCallScope
  -> Dynamic/String/Object 编组成稳定 CFFI token
  -> 调用真正的 Prime 原生函数地址
  -> 将 CFFI token 结果还原成 Dynamic/String/Object
```

加载 `*_prime` 工厂时还必须：

1. 在有效 `CallScope` 中取得工厂返回 token。
2. 通过 `val_data(token)` 提取真正的函数地址。
3. 将该地址登记为 external Prime。
4. 让 `cpp::Function` 区分普通原生函数指针与 external Prime ABI。
5. 为 external Prime 自动编组参数和返回值。

### 4.3 已落盘但尚未形成新 exe 的修正

| 修正 | 状态 | 说明 |
|---|---|---|
| Lime 恢复 typed `cpp.Callable` | 已落盘 | 移除了 hxcpp_zgc 专用 Dynamic Prime 字段 |
| CFFIMacro 恢复 `.call` typed 路径 | 已落盘 | 不再通过 `Dynamic::invoke` 调 Prime |
| hxcpp_zgc 下 `cpp.Object -> TCppDynamic` | 已落盘 | 保留 CFFI 的 scalar/string/object tag |
| `TCppFunction` 不再作为 GC 对象字段 | 已落盘 | 原生代码指针不是托管堆引用 |
| 自定义 Haxe 编译器重建 | 未执行 | 上述生成器改动尚未进入编译器二进制 |
| 全项目重新生成 | 未执行 | 当前 `export/.../obj` 仍是上一版生成结果 |
| external Prime typed ABI bridge | 未实现 | 当前最直接的代码阻塞点 |
| NDLL 模块缓存和 `hx_set_loader(hx_cffi)` | 未实现 | Lime lazy CFFI 解析所需 |
| external Prime 回归测试 | 未实现 | 现有 CFFI 测试只覆盖内部 Prime 基础路径 |
| 新一轮完整链接和启动 | 未执行 | 必须在上述代码完成后进行 |

### 4.4 接下来要修改的主要位置

- `hxcpp/include/hxcpp.h`
  - 重写 `cpp::Function<Signature, ABI>` 的 external Prime 调用路径。
  - 移除把函数代码指针误表示成 GC `RefSlot` 的 `mPtr`。
- `hxcpp/runtime/core/cffi.cpp`
  - 从工厂 token 提取真实函数地址。
  - 增加 external Prime 注册、CallScope 和 typed 参数/结果编组。
- `hxcpp/runtime/core/native_lib.cpp`
  - 缓存已加载 NDLL。
  - 新模块加载后调用 `hx_set_loader(hx_cffi)`。
- `hxcpp/tests/cffi.cpp`
  - 增加模拟外部 `__prime` 工厂、Dynamic tag、String pin 和 typed 调用回归。

## 5. 当前验证结果

### 5.1 2026-07-13 现场复测

| 检查项 | 结果 | 说明 |
|---|---:|---|
| 核心 CTest | 16/16 通过 | 总耗时 0.38 秒 |
| no-legacy 审计 | 通过 | 单一新堆，没有旧 GC/Immix/LegacyBridge 依赖 |
| Dynamic setter 生成审计 | 743/743 | `copyNonReferenceFrom` 遗漏为 0 |
| 完整生成 | 最近一轮通过 | 但尚未包含最新 Prime 类型修正 |
| 完整 C++ 编译与链接 | 最近一轮通过 | 但尚未包含最新 Prime 类型修正 |
| 应用启动 | 未通过 | 当前阻塞于 Lime external Prime ABI |
| 窗口/主菜单 | 未到达 | 不能开始最终功能验收 |
| 设置界面 | 未到达 | 此前 Null Object Reference 路径必须重新验证 |
| 实际谱面 | 未到达 | 尚无有效 FPS/帧时间结论 |
| `lime test windows` | 未闭环 | 生成、编译、链接做过，最终正常启动未通过 |
| 20 分钟稳定性 | 未执行 | 只有功能和性能门禁先通过才有意义 |

16 项核心测试覆盖：

1. GC bootstrap。
2. concurrent mark。
3. relocation。
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
15. CFFI 基础层。
16. WeakMap。

这些测试证明核心原型在当前覆盖范围内正确，不证明完整 Lime/OpenFL/FNF 兼容，也不证明真实负载下的停顿目标。

### 5.2 Windows DLL 搜索环境注意事项

构建目录使用：

```text
C:\msys64\mingw64\bin\g++.exe
```

直接从未修正的环境启动测试时，Windows 会先找到：

```text
D:\app\git\Git\mingw64\bin\libstdc++-6.dll
```

该 DLL 与测试程序不匹配，会弹出“无法定位程序输入点 `_ZSt15__get_once_callv`”并让 CTest 看似卡住。这是 DLL 搜索路径错误，不是 generated-abi 或 GC 死锁。

复测前必须先执行：

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
```

随后 CTest 为 16/16 通过。

## 6. 22 个主里程碑

各项工作量不相等；本表用于说明覆盖面，不能把“10/22”直接换算成最终完成百分比。

| # | 里程碑 | 状态 | 当前边界 |
|---:|---|---|---|
| 1 | 独立 hxcpp haxelib 与构建入口 | 基础完成 | 已能生成、编译和链接完整项目 |
| 2 | 单一新堆、无 legacy/Immix/LegacyBridge | 基础完成 | 审计通过 |
| 3 | 新对象 ABI 与彩色引用 | 基础完成 | 核心回归通过 |
| 4 | 精确 TypeDescriptor 与字段扫描 | 基础完成 | 仍需覆盖全部 native/custom 边界 |
| 5 | 精确 RootFrame、静态根、handle 根 | 基础完成 | 机器级 stack map 尚缺 |
| 6 | Region、TLAB、object-start metadata | 基础完成 | 高级空间管理尚缺 |
| 7 | SATB/insertion/forwarded-store 屏障 | 基础完成 | 仍需性能化和屏障消除 |
| 8 | 并发标记 | 基础完成 | 单 worker、remark 尚非最终版 |
| 9 | 并发重定位与 load barrier | 基础完成 | evacuation failure 和严格暂停边界尚缺 |
| 10 | Weak/Ephemeron/WeakMap/Finalizer | 当前范围完成 | soft/phantom/zombie 尚缺 |
| 11 | 新 Haxe C++ 代码生成器 | 部分完成 | 完整项目可生成，语义和 stack maps 尚在补齐 |
| 12 | Object/String/Dynamic/Array | 部分完成 | 主干可用，真实程序继续暴露边界 |
| 13 | Class/Enum/Closure/异常/反射 | 部分完成 | 启动链尚未走完 |
| 14 | CFFI/Prime/NDLL ABI | 进行中 | external Prime 是当前第一阻塞点 |
| 15 | Windows native runtime | 部分完成 | 文件、线程、TLS 等已有；移动安全审计未封口 |
| 16 | 完整游戏稳定启动 | 未完成 | 当前停在 ImageDataUtil/Prime |
| 17 | 机器级 stack/register map | 未完成 | 当前依靠精确 RootFrame |
| 18 | nursery/Survivor/promotion 分代 | 未完成 | 只有预留基础 |
| 19 | card table/remembered set | 未完成 | old-to-young 路径未实现 |
| 20 | 多 worker、调度、遥测、OOM 恢复 | 未完成 | 是后续性能和可靠性主体 |
| 21 | 完整游戏功能和性能门禁 | 未完成 | 菜单、设置、谱面、切歌、FPS/GC 关联 |
| 22 | 最长 20 分钟稳定性和跨平台 | 未完成 | Windows 优先，其他平台其后 |

按状态计数：

- 完成或基础版完成：10/22。
- 部分完成或进行中：5/22。
- 未完成：7/22。

## 7. 剩余工作必须按以下顺序推进

### P0：修通 external Prime 并继续启动

1. 实现 external Prime 地址注册和 typed ABI bridge。
2. 在 CallScope 中用 `val_data` 解包 `__prime` 工厂 token。
3. 为 Dynamic、String、对象参数和返回值实现稳定 token 编组。
4. 缓存 NDLL，并在首次加载时调用 `hx_set_loader(hx_cffi)`。
5. 增加 external Prime 回归测试。
6. 重建核心运行时并复跑 16 项测试和 no-legacy 审计。
7. 重建自定义 Haxe 编译器。
8. 全项目重新生成，审计 typed `cpp::Function` 和静态根。
9. 增量编译、链接并启动。
10. 用 GDB 捕获新的第一异常，循环修复直到窗口、菜单、设置和谱面可达。

### P1：封口 Haxe/Lime/第三方 native 兼容

- Dynamic 全部 tag、转换、比较、算术和调用组合。
- Class、Enum、Closure、匿名对象、RTTI、metadata、Reflect。
- 异常展开期间的精确根。
- 泛型 Array 和 String Unicode 边界。
- classic/Prime CFFI 全部实际签名。
- callback 线程自动 attach/detach。
- persistent handle、scoped pin 和 native identity 生命周期。
- GL、音频、视频、Discord、Lua、VLC 等第三方 native 对象。
- 阻塞 I/O 期间 Array backing 的 pin 或稳定 native buffer。

### P2：机器级精确 stack/register map

- stack map 和 register map 格式。
- 每个 safepoint 的 live reference map。
- derived pointer 的 base + offset。
- 异常展开和 callback trampoline map。
- debug verifier 与现有 RootFrame 的双轨对照。
- 验证稳定后再降低 RootScope 成本。

### P3：真正分代 GC

- Eden、Survivor、old generation。
- copying nursery 和 parallel minor evacuation。
- 对象年龄直方图和 adaptive tenuring。
- promotion reserve 和 promotion failure recovery。
- card table、线程本地 dirty-card buffer、remembered set。
- pinned/non-moving nursery 处理。

### P4：多 worker 与 work stealing

- 自适应 worker 数。
- 并行 mark、evacuation、reclaim、清零和统计。
- work-stealing deque。
- 可证明正确的 outstanding-work 终止协议。
- worker 超时/异常退出恢复。
- Windows processor group、P/E core 和 NUMA 感知。

### P5：严格低停顿与高级内存管理

- 从暂停阶段移出或切分 region/object 全量扫描。
- 有界 initial mark、remark 和 relocation flip。
- allocation debt、mutator assist 和跨帧并发调度。
- loading、interactive、idle、background、memory-pressure 模式。
- pinned、large、humongous 专用空间。
- page-run allocator 和 size-segregated recycle。
- 虚拟地址预留、commit/decommit、后台清零和 OS 内存归还。
- fragmentation scoring、evacuation reserve 和失败恢复。

### P6：遥测、帧预算和故障恢复

- GC reason 和各 phase 耗时。
- per-thread safepoint latency。
- worker 利用率。
- TLAB、nursery、promotion、card、region、pinned、large 指标。
- frame-time P50/P95/P99/P99.9/max。
- update FPS、draw FPS、heap、进程内存和 GC phase 联合时间线。
- JSON/CSV/Tracy 输出和开发 HUD。
- emergency collector、OOM、promotion/relocation/commit failure。
- queue overflow、finalizer storm、native callback race 故障注入。

### P7：最终 Windows 验收和跨平台

Windows 必须依次通过：

1. `lime test windows` 生成、编译、链接和正常启动。
2. 窗口和主菜单。
3. 设置界面，包括此前 Null Object Reference 路径。
4. 资源加载和真实谱面。
5. 暂停/恢复、多次切歌和状态切换。
6. 退出并重新进入。
7. 全程帧时间、FPS、GC 和 heap 日志。
8. 最终候选版本单轮最长 20 分钟稳定性测试。

Windows 达标后，再进入 Linux、Android、macOS 和 iOS 的同类适配及门禁。

## 8. 当前最高风险

| 优先级 | 风险 | 影响 |
|---:|---|---|
| 1 | external Prime/NDLL typed ABI 尚未完成 | 当前应用不能正常启动 |
| 2 | 完整 Haxe/Lime 语义仍有未覆盖边界 | 每越过一个故障后可能出现新的第一异常 |
| 3 | native 库保存可移动对象裸地址 | UAF、空引用、图形/音频崩溃 |
| 4 | 阻塞 I/O 使用未 pin 的 Array backing | 对象移动后地址失效 |
| 5 | remark/relocation flip 仍未严格有界 | 大堆仍可能产生明显帧尖峰 |
| 6 | 没有完整分代 nursery | 短命对象密集负载吞吐不足 |
| 7 | 当前没有完整多 worker | 大堆 mark/evacuation 吞吐不足 |
| 8 | 遥测和自动门禁未完成 | 低 FPS/长尾停顿可能不能自动归因 |
| 9 | OOM/failure recovery 未完成 | 内存压力下可能直接失败 |
| 10 | 当前工作区最新状态尚无独立快照 | 新回归时恢复粒度不够细 |

## 9. 备份与恢复点

现有关键备份：

- `D:\game\NovaFlare-backups\20260712-002120`
- `D:\game\NovaFlare-backups\20260712-005805-approved-rewrite-baseline`
- `D:\game\NovaFlare-backups\20260712-220000-rollback-fresh-zgc`
- `D:\game\NovaFlare-backups\20260712-223829-hxcpp-zgc-mark-relocation-handles`
- `D:\game\NovaFlare-backups\20260712-230747-hxcpp-zgc-concurrent-relocation-tlab`
- `D:\game\NovaFlare-backups\20260713-004506-hxcpp-zgc-haxe-frontend`
- `D:\game\NovaFlare-backups\20260713-020035-hxcpp-zgc-cffi-descriptor-prime`
- `D:\game\NovaFlare-backups\20260713-040820-hxcpp-zgc-native-thread-abi`
- `D:\game\NovaFlare-backups\20260713-111300-hxcpp-zgc-full-link-tls`

最后一个现有备份早于 18:55 的 Dynamic setter 全量再生成和成功链接，也早于当前 Prime 根因诊断及生成器修正。继续进行 runtime Prime bridge 之前，应先制作包含当前工作区的新增快照。

如果后续版本突然无法打开，恢复策略是：

1. 保留当前失败版本的日志、二进制、生成代码和调用栈。
2. 回到最近已知可验证的新 hxcpp 检查点。
3. 逐项重放已经验证的修正。
4. 不把旧 hxcpp GC 或 LegacyBridge 重新作为正式实现。

## 10. 当前恢复执行点

下一次继续时直接从这里开始：

1. 先备份当前 18:55 成功链接状态及最新 Prime 生成器修正。
2. 修改 `hxcpp/include/hxcpp.h`，实现 typed external Prime `cpp::Function`。
3. 修改 `hxcpp/runtime/core/cffi.cpp`，正确解包工厂 token 并编组参数/结果。
4. 修改 `hxcpp/runtime/core/native_lib.cpp`，加入模块缓存和 `hx_set_loader`。
5. 扩充 `hxcpp/tests/cffi.cpp`。
6. 使用正确 MinGW DLL `PATH` 重建并复测 16 项核心测试。
7. 重建自定义 Haxe 编译器并全量重新生成项目。
8. 审计生成的 NativeCFFI：
   - `fill_rect` 必须是 typed `cpp::Function`。
   - 不得再生成 Dynamic Prime 调用。
   - 不得把 `cpp::Function` 代码指针注册成 GC 静态根。
9. 增量编译、链接、启动，捕获下一处第一异常。
10. 重复到窗口、菜单、设置和谱面全部通过，再进入性能和 20 分钟稳定性阶段。

## 11. 当前可以和不可以下的结论

可以确认：

- 这是独立的新 hxcpp/GC 原型，不是 legacy GC 包装。
- 核心精确并发移动 GC 骨架存在并有 16 项回归。
- 完整 NovaFlare 已成功生成、编译和链接。
- 当前 external Prime 故障已有明确根因和修复路线。

不能确认：

- 游戏已经能正常打开。
- 设置界面和真实谱面已经通过。
- `lime test windows` 已完成最终启动闭环。
- 新 hxcpp 已经比正式 hxcpp 更快。
- GC 已经没有可感知卡顿。
- 已经达到最终 ZGC 级别的有界暂停、吞吐和故障恢复。

最简短的当前状态是：

> **新 hxcpp 已完成核心原型并成功链接完整游戏，整体约完成 30%～40%；当前正处于 Lime external Prime ABI 的启动兼容阶段，尚余约 60%～70% 的兼容、性能深化、遥测、稳定性与跨平台工作。**
