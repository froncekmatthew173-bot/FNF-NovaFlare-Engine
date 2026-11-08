# Funkin 目录与引擎入口说明

本文说明 NovaFlare Engine 的 `Funkin` 运行目录应放在哪里、OriginFunkin 与 CodeName Engine 的资源和模组如何摆放，以及如何从 NovaFlare Engine 进入对应引擎。

## Funkin 目录放在哪里
```text
assets/
manifest/
mods/
replays/
Funkin/ <---这里
```

不要把 `Funkin` 放进 NovaFlare 自己的 `assets`、`mods` 或其他子目录。

## 完整目录结构

```text
Funkin/
├─ chain.json
│
├─ OriginFunkin/
│  ├─ assets/
│  │  ├─ fonts/
│  │  ├─ preload/
│  │  ├─ shared/
│  │  ├─ songs/
│  │  ├─ sserafim/
│  │  ├─ tutorial/
│  │  ├─ videos/
│  │  ├─ week1/
│  │  ├─ week2/
│  │  ├─ week3/
│  │  ├─ week4/
│  │  ├─ week5/
│  │  ├─ week6/
│  │  ├─ week7/
│  │  └─ weekend1/
│  │
│  └─ mods-vslice/
│
└─ CodeName/
   ├─ assets/
   │  ├─ data/
   │  ├─ fonts/
   │  ├─ images/
   │  ├─ languages/
   │  ├─ music/
   │  ├─ shaders/
   │  ├─ songs/
   │  ├─ sounds/
   │  └─ videos/
   │
   ├─ addons/
   └─ mods-codename/
```

CodeName Engine 运行时可能自动创建 `.temp`，这是正常的临时目录，无需手动准备。

## 资源与模组位置

### OriginFunkin

- FNF 0.8.7 官方资源放入 `Funkin/OriginFunkin/assets/`。
- V-Slice 模组放入 `Funkin/OriginFunkin/mods-vslice/`。
- `assets` 与 `mods-vslice` 必须同级。
- 不要把 `mods-vslice` 放进 `assets`。
- 你需要去[Funkin Assets](https://github.com/FunkinCrew/funkin.assets/tree/b91f1ca5f9ee56ce1a3266c5f22cae7c4f70442b)下载所需资源
- [资源下载直链](https://github.com/FunkinCrew/funkin.assets/archive/b91f1ca5f9ee56ce1a3266c5f22cae7c4f70442b.zip)

- 模组支持：去设置打开FPS设置下方的设置以启用模组支持

### CodeName Engine

- CodeName Engine 官方资源放入 `Funkin/CodeName/assets/`。
- CodeName 模组放入 `Funkin/CodeName/mods-codename/`。
- `assets`、`mods-codename`必须同级。
- 你需要去[CodeName Engine](https://github.com/CodenameCrew/CodenameEngine/tree/98207cbd2e931ddd267ec834f67c13ccea546863)下载所需资源
- [资源下载直链](https://github.com/CodenameCrew/CodenameEngine/archive/98207cbd2e931ddd267ec834f67c13ccea546863.zip), **注意**，**下载完成后提取里面的assets**

### NovaFlare Engine 自身内容

`Funkin` 只存放外部引擎内容。NovaFlare Engine 自己的资源和模组仍使用可执行文件旁边的：

```text
assets/
mods/
```

不要把 NovaFlare 的 `assets` 或 `mods` 移入 `Funkin`。

## 游戏内入口

在 NovaFlare Engine 中依次进入：

```text
设置
└─ User Interface（用户界面）
   └─ SelectGameSubState（引擎选择）
```

打开引擎选择界面后：

1. 选择 `Origin Funkin` 或 `CodeName Engine`。
2. PC 上按确认键进入；手机上可以点击当前显示的引擎介绍图。
3. NovaFlare Engine 会保存选择并退出。
4. 再次启动 `NovaFlare Engine`，程序会进入所选引擎。

**CodeName Engine 现在同时支持 PC 与手机；手机版复用 NovaFlare Engine 的触控布局和控制设置。**

## 启动配置入口

当前启动目标记录在：

```text
Funkin/chain.json
```

其中 `preferredMode` 支持以下值：

| 值 | 启动目标 |
| --- | --- |
| `novaflare` | NovaFlare Engine |
| `origin` | OriginFunkin |
| `codename` | CodeName Engine |
| `auto` | 优先尝试 OriginFunkin；资源不可用时回到 NovaFlare Engine |

建议通过游戏内的引擎选择界面修改，不建议在程序运行时手动编辑 `chain.json`。


## 常见错误

- 错误：`Funkin/OriginFunkin/preload/`

  正确：`Funkin/OriginFunkin/assets/preload/`

- 错误：`Funkin/OriginFunkin/assets/mods-vslice/`

  正确：`Funkin/OriginFunkin/mods-vslice/`

- 错误：`Funkin/CodeName/assets/mods-codename/`

  正确：`Funkin/CodeName/mods-codename/`

- 错误：把 OriginFunkin 和 CodeName 的资源混在同一个 `assets` 中。

  正确：两个引擎各自使用自己的 `assets`。
