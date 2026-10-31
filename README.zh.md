![HEADER](./docs/img/header.png)

![CONTRIBUTORS](https://img.shields.io/github/contributors/GXDE-OS/gxde-display-manager.svg?style=plastic)
![ISSUES](https://img.shields.io/github/issues/GXDE-OS/gxde-display-manager.svg?style=plastic)
![LICENSE](https://img.shields.io/badge/license-GPLv3-blue)
![](https://img.shields.io/badge/made_with-love-red)


<!-- PROJECT LOGO -->
<br />
<div align="center">
  <!--
  <a href="https://github.com/othneildrew/Best-README-Template">
    <img src="images/logo.png" alt="Logo" width="80" height="80">
  </a>
  -->

  <h3 align="center">GXDE Display Manager</h3>

  <p align="center">
    开箱即用的登录管理器，专为GXDE OS构建
    <br />
    <a href="https://gitee.com/GXDE-OS/gxde-display-manager/wikis"><strong>查看WIKI »</strong></a>
    <br />
    <br />
    <a href="https://github.com/GXDE-OS/gxde-display-manager/releases">查看往期版本</a>
    &middot;
    <a href="https://gitee.com/GXDE-OS/gxde-display-manager/issues">提出Issue</a>
    &middot;
    <a href="https://gitee.com/GXDE-OS/gxde-display-manager/issues">请求功能</a>
  </p>
</div>


<!-- ABOUT THE PROJECT -->
## 关于本项目
GXDE Display Manager（简称`GXDM`）基于`SDDM`开发，专为GXDE OS打造，旨在提供开箱即用的登录管理与欢迎界面体验。

目前，`GXDM`默认采用Wayland环境，优先使用`gxde-wlcom`，不可用时依次回退到`LabWC`、`Sway`或`Weston`。欢迎界面本身源自DDE 15时代的Deepin `session-ui`组件，现已完成全面改造，兼容Qt6与Wayland协议栈。



### 工具链
#### 构建时
* 必要
  * Bazel/Bzlmod或CMake 3.16+
  * PAM, X11, XCB, XCursor, XFixes, XInput, XKB, XRandR, XTest
  * 一个兼容C++ 20的编译器
  * **Qt6**: Core, DBus, Gui, Widgets, Network, Qml, Quick, Svg, Xml, WaylandClient
  * **DTK**: DTK2Widget-Qt6, DTK6 Core, DTK6 DLog
  * **GSettings**: gsettings-qt6开发文件
  * **Wayland锁屏**: Qt WaylandClient 与 LayerShellQt 开发文件
* 可选
  * **Debian包构建**: Debhelper, dpkg-buildpackage

***注**: 完整的Debian打包依赖以[`debian/control`](./debian/control)文件为准。*

#### 运行时
* PAM
* logind
* D-Bus
* AccountsService
* XKB, Xcursor
* Wayland/X11环境
* 上述Qt6与DTK库
* gsettings-qt6
* Wayland锁屏优先使用 `ext-session-lock-v1`；合成器不支持时回退到
  LayerShellQt/`zwlr_layer_shell_v1` 兼容路径


<!-- GETTING STARTED -->
## 开始上手
### 前置条件
在开始前，请安装所有上述依赖包。

本程序同时支持Bazel和CMake构建。使用Bazel前请确保其在系统上可用；安装方法参见[安装Bazel](https://bazel.build/install)。

### 从源码构建（Bazel）
```bash
$ bazel build --spawn_strategy=local //:gxdm-dist
```

生成的部署归档位于`bazel-bin/gxdm-dist.tar`。

### 从源码构建（CMake）
```bash
$ cmake -S . -B build-cmake -DBUILD_WITH_QT6=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
$ cmake --build build-cmake --parallel
```


### 从源码构建（自动DEB打包）
该流程通过`debian/rules`调用CMake完成配置、测试、构建和安装。

```bash
$ chmod a+x ./build-deb
$ ./build-deb -d
```

生成的`.deb`包位于项目根目录的上级目录。

构建完成后如需清理请执行：
```bash
./build-deb -c
```

请注意这也将同时清理掉产出的`.deb`文件。


<!-- USAGE EXAMPLES -->
## 使用方式
GXDM的`.deb`包在安装时有一个供用户选择当前显示管理器的界面，可以利用该界面更新显示管理器的偏好。

GXDM的使用方式与SDDM一致，以下列出一些常用指令。

### 手动启用GXDM
```bash
$ sudo systemctl stop <其他正在使用的display manager服务名>.service
$ sudo systemctl disable <其他正在使用的display manager服务名>.service
$ sudo systemctl enable gxdm.service
$ sudo systemctl start gxdm.service
```

### 手动停用GXDM
```bash
$ sudo systemctl stop gxdm.service
$ sudo systemctl disable gxdm.service
$ sudo systemctl enable <其他欲使用的display manager服务名>.service
$ sudo systemctl start <其他欲使用的display manager服务名>.service
```

### 手动重启GXDM
```bash
$ sudo systemctl restart gxdm.service
```

### 验证运行状态
```bash
$ systemctl status gxdm.service --no-pager
$ sudo journalctl -u gxdm.service -b --no-pager
```

### D-Bus接口
GXDM通过会话总线提供锁屏快捷键和全局Greeter外观设置。应用程序应使用以下公开接口：

| 项目 | 值 |
| --- | --- |
| 总线 | Session bus |
| 服务 | `top.gxde.DisplayManager` |
| 对象路径 | `/top/gxde/DisplayManager` |
| 接口 | `top.gxde.DisplayManager` |

| 方法 | 说明 |
| --- | --- |
| `TryEnrollLkScr(bool enabled) -> bool` | 尝试注册或注销Super+L GXDM锁屏快捷键。已有程序占用Super+L时不会抢占。 |
| `UnregisterLkScr()` | 取消Super+L注册。 |
| `LkScrStat() -> bool` | 返回Super+L是否已由GXDM成功注册。 |
| `Show()` | 显示GXDM锁屏界面。 |
| `SetCursor(string theme) -> bool` | 设置全局Greeter光标主题。主题必须已经安装。 |
| `SetWallpaperGXDEDefault() -> bool` | 将全局Greeter壁纸恢复为当前GXDE默认壁纸。 |
| `SetWallpaperDDELockDefault() -> bool` | 将全局Greeter壁纸设置为程序内置的DDE锁屏默认背景。 |
| `SetWallpaper(string path) -> bool` | 使用本地路径或`file://` URL设置全局Greeter自定义壁纸。 |
| `SetLockWallpaperOverride(string path) -> bool` | 使用本地路径或`file://` URL设置当前用户的锁屏壁纸override。 |
| `ClearLockWallpaperOverride() -> bool` | 清除当前用户的锁屏壁纸override。 |
| `LockWallpaperOverride() -> string` | 返回当前用户持久化的锁屏壁纸override。 |
| `SetGreeterDisplayServer(string displayServer) -> bool` | 持久化Greeter显示服务器。有效值为`wayland`、`x11`和`x11-user`。 |
| `GreeterDisplayServer() -> string` | 返回当前持久化的Greeter显示服务器。 |
| `SetNeoGreeterEnabled(bool enabled) -> bool` | 是否启用新版 QML greeter，默认 true；false 切回经典版。 |
| `NeoGreeterEnabled() -> bool` | 返回已保存的新版 greeter 开关。 |
| `SetLockScreenStyle(string style) -> bool` | 通过系统守护进程保存锁屏样式（`classic` 或 `modern`）；对应程序未安装时返回失败。 |
| `LockScreenStyle() -> string` | 返回当前生效的锁屏样式。 |

锁屏服务默认在所有系统尝试注册 Super+L；已被其他程序占用时不抢占。

新版 greeter 开关（系统总线）：

```sh
busctl call top.gxde.DisplayManager /top/gxde/DisplayManager \
  top.gxde.DisplayManager.System SetNeoGreeterEnabled b false
busctl call top.gxde.DisplayManager /top/gxde/DisplayManager \
  top.gxde.DisplayManager.System NeoGreeterEnabled
```

开关保存为 `gxdm.conf` 的 `[General] EnableNeoGreeter`，下次启动 greeter 生效。传入 `true` 可重新启用。成功保存后发出 `NeoGreeterEnabledChanged(bool)` 信号。新版程序缺失时回退经典版。可用 `gxdm-greeter-neo --test-mode` 预览。虚拟键盘需要同时安装 Qt Virtual Keyboard 的 QML 模块和输入法插件。

### QML 锁屏

`gxdm-lock-neo` 是基于 Qt Quick 的会话内锁屏前端。它复用 GXDM 的 PAM
认证、用户枚举、锁屏快捷键注册和 D-Bus 接口，因此现有的桌面快捷键、
电源菜单以及 `org.freedesktop.ScreenSaver` 调用都能继续工作。Wayland
下优先使用 `ext-session-lock-v1`，不可用时回退到 LayerShellQt；X11 下
创建全屏窗口并抓取键盘。

通过 `/etc/gxdm.conf` 的 `[General] LockScreenStyle` 选择样式：

```ini
[General]
LockScreenStyle=modern
```

`modern`（默认）启动 `gxdm-lock-neo`，`classic` 启动
`gxdm-lock-classic`；未安装 `gxdm-lock-neo` 时使用 `classic`。为兼容旧配置，当 `gxdm.conf` 中没有该项时，会读取
`/etc/gxdm/OMG.conf` 的 `[NextFeatReq] SwitchLkScrStyle`。经典锁屏的
自启动和 D-Bus 服务入口保持不变：当配置为 modern 且 `gxdm-lock-neo`
与 `gxdm-lock-classic` 安装在同一目录时，经典进程会以相同参数 exec
新版前端；若新版程序不存在，则继续使用经典锁屏。

也可以在运行时通过会话总线切换（系统总线 `top.gxde.DisplayManager.System`
上有同名方法）：

```sh
busctl --user call top.gxde.DisplayManager /top/gxde/DisplayManager \
  top.gxde.DisplayManager SetLockScreenStyle s modern
```

守护进程写入 `[General] LockScreenStyle` 并发出
`LockScreenStyleChanged(string)` 信号。正在运行的锁屏会等到解锁后，释放自己的
D-Bus 名称，再以 `--daemon` exec 另一种锁屏，切换过程中不会让会话处于未锁定状态。

使用 `-DBUILD_NEO_LOCK=ON`（默认开启，需要
`BUILD_CLASSIC_GREETER=ON`）构建。可在不真正锁定会话的情况下预览：

```sh
./build-cmake/src/greeter-classic/gxdm-lock-neo --test-mode
./build-cmake/src/greeter-classic/gxdm-lock-neo --test-mode --switch
```

预览模式接受任意密码，按 `Esc` 退出。生产环境使用 `--daemon`
（自启动/D-Bus）启动并等待锁屏请求，或直接启动以立即锁定。使用
`-DLOCK_BUILD_TESTS=ON` 可启用锁屏后端测试，CTest 名称为
`LockBackend` 和 `LockStyle`。


**注意：**Greeter 只在显式配置了全局壁纸（`state.conf` 的 `[Greeter] Wallpaper` 非空）时才使用这份全局壁纸；未配置时新版 Greeter 会跟随当前用户的壁纸链，与锁屏保持一致。锁屏/用户壁纸优先级为：当前用户的`LockWallpaperOverride`、当前用户的Deepin `GreeterBackground`/`BackgroundFile`、Deepin `DesktopBackgrounds`（与 GSettings 桌面壁纸同源）、AccountsService、GSettings桌面壁纸、全局Greeter壁纸/默认壁纸。所有壁纸设置在持久化成功后立即刷新当前Greeter或锁屏。自定义图片和override必须是可识别的图像且不大于128 MiB；全局Greeter壁纸复制到`~gxdm`。用户锁屏override会保存两份：`~/.local/share/gxdm/lock-wallpaper-override`供会话内锁屏使用，`~gxdm/lock-wallpaper-override-<uid>`供Greeter使用（即使用户主目录不可进入，例如权限为`0700`，Greeter也能读取），均不依赖原始图片继续存在。`SetGreeterDisplayServer`会在下次创建Greeter显示时生效。

常用调用示例：

```bash
# 注册Super+L
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  TryEnrollLkScr b true

# 取消Super+L注册
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  UnregisterLkScr

# 查询Super+L状态
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  LkScrStat

# 设置Greeter光标主题
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  SetCursor s gxde

# 设置自定义Greeter壁纸
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  SetWallpaper s /绝对路径/壁纸.jpg

# 恢复GXDE默认Greeter壁纸
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  SetWallpaperGXDEDefault

# override锁屏壁纸
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  SetLockWallpaperOverride s /绝对路径/锁屏壁纸.jpg

# 清除锁屏壁纸override
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  ClearLockWallpaperOverride

# 使用X11 Greeter显示服务器
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  SetGreeterDisplayServer s x11

# 查询Greeter显示服务器
busctl --user call top.gxde.DisplayManager \
  /top/gxde/DisplayManager top.gxde.DisplayManager \
  GreeterDisplayServer
```

GXDM守护进程还在system bus上拥有同名服务，其内部接口为`top.gxde.DisplayManager.System`。该接口负责把全局外观设置写入`~gxdm/state.conf`，并将Greeter显示服务器写入`/etc/gxdm.conf`；其中全局自定义壁纸使用Unix FD而不是用户提供的路径。session bus接口会把用户锁屏override保存在用户数据目录，并转发一份给system bus接口保存到`~gxdm`，以便Greeter读取。system bus接口是实现细节，普通应用不应直接调用。


<!-- ROADMAP -->
## 里程碑
- [x] 新增DEB打包
- [x] 移植DDE Greeter
- [x] 新增Waylanbd greeter支持
- [x] 新增CMake支持
- [x] 新增屏幕键盘支持
- [x] 新增Archlinux打包

请参阅[开启的Issues](https://gitee.com/GXDE-OS/gxde-display-manager/issues)以获取当前所有请求的新功能。



<!-- CONTRIBUTING -->
## 贡献
我们感激每一位贡献者，如果您有让GXDM更好的建议或者想要请求功能，欢迎您提出新Issue。您也可以fork本仓库并创建拉取请求。

### 打包注意事项
如果您想为其它发行版打包，请记得让包管理器将`./data/configs/OMG.conf`安装至`/etc/gxdm`。

### GXDM的贡献者们
***注**: GXDM基于SDDM，原版SDDM的贡献者信息可以在[这里](https://github.com/sddm/sddm/graphs/contributors?all=1)找到*

<a href="https://github.com/GXDE-OS/gxde-display-manager/graphs/contributors?all=1">
  <img src="https://contrib.rocks/image?repo=GXDE-OS/gxde-display-manager" alt="contrib.rocks image" />
</a>


<!-- LICENSE -->
## 许可证
本项目以[GNU GENERAL PUBLIC LICENSE Version 3](./LICENSE)许可发行。

本项目含有来自GXDE `session-ui`的源码，可以在`src/greeter-classic`找到，原版上游位于[GXDE-OS/gxde-session-ui](https://gitee.com/GXDE-OS/gxde-session-ui)，以[GNU GENERAL PUBLIC LICENSE Version 3](https://gitee.com/GXDE-OS/gxde-session-ui/blob/master/LICENSE?svcp_stk=1_ea4aETwwgfjPoBNPiiCDEDSV5zkBHs2UhWwbnE9dOUMgTQpn3GWWsP7g-d5P7vdOc8pUy6n3gqUzBwYWRSpsMn5glSx_AwPrOegiEQB6-z-bFaJX84-951qO7y66p6b5okTkZXtJkAnfclQjppYXX55voA3ESpiytcTqZIN1qUEqZKeRHjnERUf83GXnqoXUeXTWmfPGOafIokUE6EkrfA%3D%3D)获得许可，除非在文件头部另行说明。

本项目含有[CharOfString/samoyed-greeter](https://gitee.com/CharOfString/samoyed-greeter)的部分源码，以[GNU GENERAL PUBLIC LICENSE Version 3](https://gitee.com/CharOfString/samoyed-greeter/blob/main/LICENSE)获得许可，除非在文件头部另行说明。

本项目使用[Maven Pro](https://github.com/googlefonts/mavenproFont)作为锁屏字体，其以[SIL Open Font License 1.1](https://github.com/googlefonts/mavenproFont/blob/main/OFL.txt)获得许可。


<!-- ACKNOWLEDGMENTS -->
## 鸣谢
GXDM的开发离不开以下项目提供的参考：
* **SDDM**: https://github.com/sddm/sddm
* **LightDM**: https://github.com/ubuntu/lightdm
* **GXDE Session UI**: https://gitee.com/GXDE-OS/gxde-session-ui
* **Samoyed Greeter**: https://gitee.com/CharOfString/samoyed-greeter
* **BEST-Readme-Template**: https://github.com/othneildrew/Best-README-Template
* **SilentSDDM**: https://github.com/uiriansan/SilentSDDM
