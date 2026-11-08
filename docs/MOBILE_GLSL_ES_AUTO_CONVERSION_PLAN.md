# NovaFlare 移动端 GLSL ES 自动转换实施清单

## 目标

NF 在手机端运行 OpenGL ES，而部分 Mod/OpenFL 运行时着色器使用桌面 GLSL 写法，直接提交给手机驱动时会编译失败。移动端应在 OpenFL 最终编译着色器之前，根据真实上下文自动选择 GLSL ES 1.00（OpenGL ES 2）或 GLSL ES 3.00（OpenGL ES 3+），同时允许用户关闭该行为。

“完整转换”的实现标准：不使用会误伤注释、宏或变量名的全局字符串替换；转换器必须识别词法状态、Shader 阶段、全局声明和预处理行，保持 uniform/OpenFL 内建名称不变，并且重复转换仍得到相同结果。桌面 GPU 功能在目标 ES 标准中确实不存在等价语义时，必须给出带阶段、行号和原因的明确诊断，不能静默产出错误画面。

## 设置项

- `autoShaderConversion`：移动端专属，默认开启。决定是否在 OpenFL 编译边界自动转换 GLSL 为 GLSL ES。
- `mouseTrailEffect`：移动端专属，默认开启。决定是否显示全局鼠标/触摸移动星星拖尾；点击反馈不受影响。
- 两项设置都进入 `ClientPrefs` 自动保存，并能在移动端设置界面即时更新运行时开关。
- Freeplay 的性能保护仍可临时压制拖尾；退出 Freeplay 时恢复用户设置，而不是强制开启。

## 着色器转换范围

1. 在项目实际覆盖 OpenFL 的 `source/openfl/display/Shader.hx` 最终 GL 编译入口统一转换，覆盖 Haxe Shader、`FlxRuntimeShader`、Lua/HScript Mod Shader 和 `ShaderFilter`。
2. 只在移动端、当前上下文为 OpenGL ES、且 `autoShaderConversion` 开启时转换；桌面端完全保持原样。
3. 根据实际上下文选择目标：
   - OpenGL ES 3.x：输出 GLSL ES 3.00；阶段正确地转换 `attribute/varying`、旧纹理函数、`gl_FragColor/gl_FragData`、桌面版本及兼容限定符。
   - OpenGL ES 2.x：输出 GLSL ES 1.00；转换顶层现代 `in/out`、显式片元输出、可降级的 `layout`、现代纹理调用，并按需处理 derivatives/texture-lod 扩展。
4. 清理输入中的桌面或旧 ES `#version`，确保目标 `#version` 为首条指令；规范化 `#extension` 顺序并补充阶段所需的 float/int/sampler 精度。
5. 词法扫描跳过行注释、块注释和字符串；预处理宏的替换体仍参与兼容转换，保留换行以维持驱动编译日志行号。
6. ES 2 路径对 `texture()` 通过 sampler 类型或兼容重载正确落到 `texture2D/textureCube`；对整数 sampler、UBO、MRT、位运算、`texelFetch` 等无等价能力生成明确诊断。
7. 转换器保持 uniform 名称和 OpenFL 内建变量名称不变，避免破坏脚本的 `setShaderFloat`、`setShaderSampler2D` 等调用。
8. 转换应幂等；Program 缓存键包含目标版本、转换器 ABI 和能力配置，运行时 revision 只负责失效。设置切换时使已有 Shader 在下一次启用时重建，不能继续复用旧 Program，也不能因反复切换无限制造缓存键。
9. 对桌面驱动可接受、但 ESSL 1.00/3.00 禁止的“使用 uniform/varying 的全局运行时初始化”，保留全局声明，在原条件编译分支生成初始化函数，并在真实入口开头按声明顺序调用；宏当时的值、`#if/#else` 活动分支和驱动报错行号都必须保持。
10. 转换只是兼容层：编译/链接前先静默试编译转换后的源码，被驱动拒绝时自动回退到转换前的原始 OpenFL GLSL（warn 记录驱动信息），两者都失败才走原来的报错/弹窗路径；回退得到的 Program 仍按原缓存键复用。

## 计划修改位置

- `source/openfl/display/Shader.hx`：统一转换入口、版本失效和最终 Program 缓存接入（这是 `Project.xml` 实际优先使用的 OpenFL 覆盖文件）。
- `source/openfl/display/OpenGLRenderer.hx`：项目内可追踪的 OpenFL 覆盖；保证开关/上下文变化后按照“重建 → 绑定 → sampler 启用”的顺序切换 Program。
- `source/flixel/addons/display/FlxRuntimeShader.hx`：项目内可追踪的 flixel-addons 覆盖；让运行时 Shader 正确反射带精度限定符和 `layout(...) in` 的输入。
- `source/general/shaders/MobileShaderConverter.hx`：GLSL/GLSL ES 的分阶段转换规则。
- `source/general/shaders/flixel/system/FlxShader.hx`：让 NF 自定义 FlxShader 复用同一转换器。
- `source/general/backend/ClientPrefs.hx`：两个移动端默认值、启动应用与持久化。
- `source/options/groupData/GeneralGroup.hx`：移动端设置项及即时回调。
- `source/general/objects/screen/MouseEffect.hx`、`source/states/freeplayState/FreeplayState.hx`：拖尾用户开关与页面临时压制协作。
- `assets/shared/language/*/options/General.lang` 与 `optionTips/General.lang`：中英葡显示文本。

## 验收清单

- [x] 静态接入确认：桌面构建不会启用或显示移动端转换设置。
- [x] 静态默认值确认：Android/iOS 首次启动时两个设置均为开启。
- [x] 静态路径确认：关闭自动转换后，新编译的 Shader 使用原始 OpenFL GLSL 路径。
- [x] 转换规则确认：ES 2 与 ES 3 分阶段处理，转换标记保证同一目标重复转换幂等，词法扫描跳过注释和字符串并支持续行宏。
- [x] 全局初始化确认：ES 2/ES 3 共用 ABI 3 下沉路径，支持 `#define mainImage main`，并用分支内 guard 保持条件编译语义。
- [x] 宏声明边界确认：宏直接生成整条全局声明而无法安全展开时给出源码行诊断，不做可能改变语义的半转换。
- [x] 诊断路径确认：目标 ES 标准无法表达的类型、函数、接口块、layout 或输出会记录阶段、行号和原因，并保留非法语义交给驱动拒绝，而不是猜测替代。
- [x] 接入确认：Lua/HScript 创建的 `FlxRuntimeShader` 同样经过统一入口。
- [x] 回退确认：转换后的源码被驱动拒绝（编译或链接失败）时静默回退到原始 OpenFL GLSL，仅 warn 记录；原始源码同样失败才复用原来的报错/弹窗路径，`ErrorHandledShader` 的覆盖行为不变。
- [x] 逻辑确认：关闭鼠标拖尾后清理现有 TrailEffect 且停止生成新拖尾，点击反馈路径保持独立。
- [x] 逻辑确认：Freeplay 的页面级抑制与用户开关取 AND，退出后不会覆盖用户选择。
- [x] Haxe/Markdown、条件编译接入、语言键和目标文件 Git whitespace 静态检查通过（未调用编译器）。
- [ ] Android 真机编译、Shader 编译日志和视觉效果由用户完成最终验证。
