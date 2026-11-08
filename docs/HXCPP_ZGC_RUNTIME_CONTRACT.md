# hxcpp 单堆精确低停顿运行时契约

日期：2026-07-12  
状态：实现约束；任何代码与本文冲突时，代码必须修改或写出经过测试的数据化设计决议。

## 1. 身份与边界

- 对外 haxelib 名称、命令入口和 Haxe C++ target 名称保持 `hxcpp`。
- 内部是全新的对象 ABI、GC、标准对象模型、代码生成器和构建前端。
- 旧 hxcpp、Immix、LegacyBridge、保守栈扫描和双堆桥接只能作为独立基准，不能成为新产物的依赖或回退路径。
- 正式进程中只能存在一个托管堆和一套对象所有权规则。
- 构建和静态审计必须拒绝旧分配、旧 mark/visit、旧 write barrier 与跨堆符号。

## 2. 精确性不变量

- 所有堆引用只允许出现在类型描述符声明的槽、stack map/root frame、静态根表或 handle table 中。
- 任意整数、未知 native 内存和未声明栈字节永远不能保活对象。
- 对象描述符覆盖继承字段、Array/Dynamic/String/闭包/Enum 的变长或自定义追踪逻辑。
- 每个可能分配、阻塞、调用未知 native 代码或轮询回收的点都有精确 safepoint 信息。
- 派生指针保存为“基对象 handle + offset”，不得作为不可更新裸地址跨 safepoint 存活。
- debug verifier 会用独立遍历校验描述符、屏障、forwarding 与 region object-start map。

## 3. 低停顿不变量

- 正常回收不得包含与整个堆大小线性相关的 stop-the-world 阶段。
- initial root snapshot、remark 和 relocation-set flip 必须是有界握手，并记录每线程到达延迟。
- 标记、重定位复制、reclaim、清零、统计和 decommit 默认在 mutator 运行时并发执行。
- load barrier 必须识别当前颜色、解析 forwarding chain，并用 CAS 自愈引用槽。
- store barrier 同时满足 SATB、增量更新以及后续分代 remembered-set 需求。
- relocation 期间写入不得丢失：写者必须写入新副本，或参与一个可证明线性化的转发协议。
- 任何队列终止都必须以可证明的 outstanding-work 协议实现，并有超时与故障注入测试。

## 4. 引用颜色与 ABA

- 托管引用使用显式 view/color；空引用恒为零。
- 每次颜色复用前必须精确重染所有 roots、handles 和 heap slots，或使用足够宽的 epoch 证明不会 ABA。
- load barrier 必须遍历完整 forwarding chain，不能假定最多移动一次。
- 连续多轮 compact、颜色回绕、并发 store 与 slot self-heal 必须有独立回归测试。

## 5. 分配与 region

- 小对象采用每线程 TLAB bump allocation；共享路径只负责批量补充 TLAB。
- region 在发布给其他线程前，创建线程必须原子保留自己的首段，防止两个线程获得同一地址。
- region 维护 object-start metadata、状态机、live bytes、pin count 和 relocation epoch。
- large/humongous、pinned 和普通可移动对象使用明确的 region/space 策略。
- 不能通过永久 pin 代替缺失的精确 root、写屏障或 native handle 迁移。

## 6. Haxe 与 native ABI

- Object、Dynamic、String、Array、Enum、Class、匿名对象和闭包全部属于同一新堆。
- 生成器为类输出稳定 TypeDescriptor，为函数输出精确 stack map/root metadata。
- 字段、数组、静态字段、构造期写入、反射写入和 native 写入全部经过新屏障。
- CFFI 只能持有 strong/weak/pinned/scoped handle；未注册 `hx::Object*` 不得跨调用保存。
- native blocking 状态、callback thread attachment、异常展开和 finalizer 线程都有显式协议。

## 7. 强制失败回归

下列历史故障均必须由自动化测试覆盖：

- TLAB 首段在 region 发布前未保留导致的重叠分配。
- 颜色复用后旧 Remapped 引用绕过 forwarding 的 ABA。
- legacy raw mark 把新对象头解释为旧 Immix metadata。
- 数组扩容后保存旧元素槽地址形成悬空 root。
- 永久 pinned handles/owner 表无界增长。
- `dirty cards × pinned objects` 或弱句柄成员查询的二次复杂度。
- evacuation worklist 错误终止导致永久无响应。
- GC 期间闭包捕获、Dynamic、String、Array 或设置界面对象出现空引用。
- 应用仍响应但 update/draw FPS 长期低于门槛而监控未判失败。

## 8. 阶段门禁

每个阶段必须同时具备：源代码、可重复构建、正确性测试、压力测试、no-legacy 审计和原始日志。最终门禁还包括：

- `lime test windows` 完整生成、编译、链接和启动。
- 设置界面、菜单、加载、实际谱面、切换状态和退出重进路径。
- 每秒 update/draw FPS、frame-time histogram、GC phase 与 heap telemetry 关联。
- 单轮最长 20 分钟稳定性测试；SEH、空引用、无响应、持续低于 FPS 门槛或内存无界增长均失败。

“能启动”不代表完成，“平均暂停下降”也不能掩盖 P99.9、最大帧停顿或正确性回归。
