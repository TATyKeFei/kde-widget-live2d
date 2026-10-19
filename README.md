# KDE Live2D 桌面伙伴

这是一个可以将你的 live2d 模型放到桌面上性能强现代好用的部件

使用 **QML + C++ + Live2D Cubism Native SDK**，支持 Wayland，提供 OpenGL 与原生 Vulkan 两种角色渲染后端

还有，这个项目使用了 Ai Vibe，因为我不会且看市面上的都不太好用

我又有这个需求约了 live2d 模型玩不直播丢一边感觉好浪费，就开源给想用的一起用了

赶紧来个大佬吧，这玩意好烧token啊👺

# 特色功能

| 功能         | 作用                           | 备注 |
| :---         | :---                           | :--- |
| 视线跟随鼠标 | 让你的老婆盯着鼠标指针看       |      |
| 更多  |

# 支持版本

| 项目     | 支持版本                    | 说明   |
| :---     | :---                        | :---   |
| 桌面环境 | KDE Plasma 6.8+ 及以上      |        |
| QT       | Qt 6.5+                     |        |
| 架构     | x86_64                      | Arm好像支持，但由于没有设备无法测试 |
| 显示服务器 | 仅 Wayland                | 不支持 x11，因为 KDE 也不支持了     |
| 图形后端 | Vulkan 1.3+ / OpenGL EGL    |        |
| 支持格式 | 支持 `.model3.json` 模型    | 不支持 Cubism 2 的 `.model.json`    |

# 安装部件

在 Release 下载完小部件后直接正常安装小部件就好了

## 如你想尝鲜玩玩开发版本

在本仓库 **Actions → Build plugin → 最新成功的运行 → Artifacts** 下载 `kde-widget-live2d-archlinux-x86_64`

解压`.zip`再解压`.tar.gz`后，进入目录执行

```sh
bash install.sh
```

# 预览

做完先

# 构建 

> 以下Ai写的

需要 CMake 3.22+、C++17、Qt 6 Quick / QML / Quick Controls / Quick Dialogs / DBus、LayerShellQt、GLEW、OpenGL / EGL、Vulkan 头文件与 loader、`glslc`。测试需要 Qt Test；真实模型测试需要 Python 3.9+。运行小部件还需要 Plasma 6 和 Kirigami。

Arch Linux 开发依赖：

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-declarative glew libplasma kirigami layer-shell-qt vulkan-headers vulkan-icd-loader shaderc
```

另外安装显卡对应的 Vulkan 驱动。可选的 `vulkan-swrast` 能在没有 GPU 的测试环境中提供 Mesa lavapipe；`vulkan-validation-layers` 用于检查 Vulkan 调用。

从 [Live2D 官方页面](https://www.live2d.com/en/sdk/download/native/)获取 **Cubism SDK for Native 5 R5** 并解压。SDK 留在仓库外；构建不自动下载，也不把 SDK 和示例角色打进小部件。

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCUBISM_SDK_ROOT=/absolute/path/to/CubismSdkForNative-5-r.5 \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j
ctest --test-dir build --output-on-failure
```

默认编译两种渲染后端。可加 `-DLIVE2D_BUILD_VULKAN=OFF` 仅编译 OpenGL；这种构建仍可在 Qt Quick Vulkan 后端下显示，自动模式会使用 OpenGL 渲染角色。

默认使用系统 GLEW。若 GLEW 的 GLX 初始化影响 EGL，可下载 GLEW 2.2.0 源码并加 `-DGLEW_SOURCE_ROOT=/absolute/path/to/glew-2.2.0`，项目会按 EGL 模式编译它。

SDK R5 的 Vulkan 路径需要修正着色器加载、深度清除值、深度图像布局、屏障目标阶段和资源销毁。构建通过 [cmake/CubismVulkanFixes.cmake](cmake/CubismVulkanFixes.cmake) 在构建目录生成修正后的源文件，不修改外部 SDK。着色器嵌入各自的 helper 程序，运行不依赖 SDK 目录或工作目录。切换 SDK 版本前需重新审查这些修正。

## GitHub Actions 自动构建

[Build plugin](.github/workflows/build.yml) 在推送、Pull Request 和手动运行时执行，无需配置 Secrets。在 GitHub Actions 页面选择 **Build plugin → Run workflow** 也可以手动构建。

流程使用 Arch Linux 容器安装依赖，下载固定版本的 **Cubism Native 5 R5 / GLEW 2.2.0** 并校验 SHA-256，再编译 OpenGL、原生 Vulkan 后端和预览程序。GLEW 从源码按 EGL 模式静态编译，避免依赖 GLX 初始化。测试使用 Mesa 软件 OpenGL 和 lavapipe，并使用 SDK 的 Haru 模型验证真实渲染；测试模型不会进入安装包。

测试通过后上传 `.tar.gz` 安装包和校验文件，保留 14 天；失败时上传测试日志供排查。包中包含小部件、QML 原生插件、两个渲染程序、预览程序、安装脚本及第三方许可。流程只上传 Actions 构建产物，不自动创建 GitHub Release。

脚本也可以本地使用，需先安装上述开发依赖：

```sh
bash scripts/ci-fetch-deps.sh /tmp/live2d-deps
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
  -DCUBISM_SDK_ROOT=/tmp/live2d-deps/CubismSdkForNative-5-r.5 \
  -DGLEW_SOURCE_ROOT=/tmp/live2d-deps/glew-2.2.0
cmake --build build -j
bash scripts/package-bundle.sh build dist \
  /tmp/live2d-deps/CubismSdkForNative-5-r.5 /tmp/live2d-deps/glew-2.2.0
```

SDK/GLEW 的下载地址及摘要固定在 `scripts/ci-fetch-deps.sh`；升级 SDK 时需要同步审查 Vulkan 修正。自动下载仅用于 CI 和显式调用此脚本，普通 CMake 构建仍使用你指定的 SDK 目录。

## 预览模型

```sh
./build/live2d-preview /absolute/path/to/Haru/Haru.model3.json
./build/live2d-preview --render-backend vulkan /absolute/path/to/Haru/Haru.model3.json
./build/live2d-preview --render-backend opengl /absolute/path/to/Haru/Haru.model3.json
```

不带路径启动时，可在窗口中选择模型。滚轮和底部滑条缩放；菜单可切换表情、暂停动画、选择渲染后端，旁边显示实际后端。模型旁边的纹理、动作、物理文件必须保留目录结构；资源必须位于所选模型目录内。

小部件设置中的帧率支持 **1–240 FPS**，设为 **0（无限制）** 后不设固定帧率上限。限帧使用固定时间轴，定时器唤醒稍晚时不会把误差累积到后续帧，显示或渲染阻塞时跳过错过的时刻，不突发补帧。每个角色最多保留一张待显示图像和一帧预取请求／响应，让助手渲染与 Qt Quick 显示重叠。待显示图像未被取走时，预取帧完成后停止发新请求，避免无限堆积或覆盖；暂停或隐藏时不再发起连续渲染。预取可能增加最多一帧输入延迟。实际生成速度取决于显卡、角色复杂度、图像传输和桌面显示速度。

Qt 6.6+ 且构建环境提供匹配的 `Qt6::GuiPrivate` 开发模块时，显示端使用 QRhi 更新同一张 GPU 纹理，只在尺寸变化时重新分配，减少每帧创建、销毁纹理造成的抖动。该显示路径兼容 Qt Quick 的 OpenGL 和 Vulkan 后端；软件显示后端、旧版 Qt 或缺少开发模块时保留普通图像纹理路径。使用 QRhi 的构建须匹配运行时 Qt 版本。

屏幕位置跟随通过 KWin 每 8 ms 采样一次最新指针，避免原来的约 30 Hz 指针采样与角色帧率互相错开。D-Bus 同时最多一条请求，鼠标静止时每秒只发送一次保活；相对位移模式直接使用输入设备事件。

OpenGL 和 Vulkan 都使用 Mipmap 与三线性过滤，改善角色缩小时细线、透明轮廓的锯齿和闪烁。Vulkan 在加载纹理时按预乘 alpha 生成完整缩略层，不依赖显卡的线性 blit 支持；增加约三分之一纹理显存及一次加载开销。当前渲染目标仍是单采样，尚未启用 MSAA 或超采样，网格边缘及很小尺寸下的细节仍可能出现锯齿。

设置或右键菜单中的 **始终置顶** 会把角色移到独立透明悬浮窗口。Wayland 使用 LayerShellQt 的 overlay 层，覆盖普通应用和全屏窗口，不占用工作区，也不会在显示时主动抢焦点；单击后可使用聚焦按键。鼠标移到角色窗口上方时显示拖动按钮和菜单，可在当前屏幕内移动，或选择“返回桌面（取消置顶）”。每个小部件独立切换；桌面原位置保留返回按钮，关闭置顶后恢复在小部件中渲染。悬浮位置当前不跨重启保存，切换回置顶时从小部件位置开始。

可在“功能”设置或小部件右键菜单开启 **鼠标穿透**，桌面角色和始终置顶的悬浮角色都支持。桌面角色将点击和滚轮交给下方桌面内容，仅禁用该小部件的输入，不修改整个桌面窗口；悬浮角色通过窗口输入属性穿透到下方窗口。开启后不能点击、拖动角色或使用聚焦按键，全局跟随和“整个桌面”动作按键仍可使用。桌面角色需要先右键桌面进入 **编辑模式**，交互会临时恢复，再打开小部件设置关闭穿透；悬浮角色可从原桌面小部件设置关闭。默认关闭，每个角色独立保存，与 OpenGL／Vulkan 渲染后端无关。

若模型没有 `Idle` 动作组，会尝试读取同目录、同名的 `.vtube.json` 中的 `FileReferences.IdleAnimation`，支持模型目录或 `animations/` 下的动作文件。无需修改原模型；已有 `Idle` 组优先。没有姿态文件的模型也会应用动作中的 `PartOpacity`，避免 Hiyori 等角色的备用手臂同时显示。同时导入 `.vtube.json` 中 `TriggerAnimation`、`ToggleExpression` 的动作和表情引用，支持按键组合、表情切换及按住/松开表情。没有待机动作的模型不会随机播放按键动作；可通过按键、右键菜单的“随机动作”，或预览窗口的动作列表播放。

同时读取 `.vtube.json` 的 `ParameterSettings` 中 **MousePositionX / MousePositionY** 映射，例如 p298 的 `ParamMouseX / ParamMouseY`，让手和模型里的鼠标随桌面指针移动。沿用指针跟随的关闭、聚焦应用窗口、前台窗口非全屏、始终跟随四种策略，不需要全局按键的 evdev 读取权限。鼠标位置以角色所在屏幕的中心为零点，边缘为 ±1，多屏越界时保持在边缘；没有桌面追踪连接时，“始终跟随”退回角色区域内的鼠标移动。支持映射范围、反向范围、输入/输出限幅，最终限制在模型参数范围内；平滑按配置强度和经过时间近似实现，帧率改变不会改变响应速度，与 VTube Studio 的滤波效果可能有差异。已映射的视线参数不会重复叠加默认跟随；关闭或跟随策略不满足时，映射立即停止，由模型动作和表情控制姿势。没有鼠标映射的模型继续使用默认视线跟随。

FPS 游戏锁定屏幕指针时，在设置中将 **鼠标移动** 改为 **相对位移（FPS 游戏）**，全屏游戏配合 **指针跟随 → 始终跟随**，需要显示在游戏上方时开启 **始终置顶**。该模式读取 evdev 的 `REL_X / REL_Y`，用有边界的虚拟位置驱动同一套鼠标映射和视线，屏幕指针无需移动；不抓取设备，不修改游戏输入。停止移动 120 ms 后逐渐回中；可调整 10%–500% 灵敏度，默认约 500 个设备计数从中心移到边缘，实际手感取决于鼠标 DPI。设备事件按 `SYN_REPORT` 合并，丢包或设备断开会复位；暂停、隐藏、切换模式或跟随策略不满足时停止响应并复位。相对移动与“模型动作按键”独立，按键关闭时也能使用；须先运行下面的输入设备授权脚本，已有当前鼠标权限则无需重复授权。只有屏幕位置模式免除此权限。相对移动加“始终跟随”无需 KWin 指针连接；其他跟随策略仍依赖 KWin 判断前台窗口状态。

模型动作按键可在设置中选择 **关闭 / 聚焦小部件时 / 整个桌面**。默认聚焦模式，单击角色后响应按键。整个桌面模式在其他应用中也会触发动作，不拦截或注入按键。暂停、隐藏、切换模式，以及聚焦模式下丢失焦点时，会释放按住的表情。

Wayland 下的全局按键通过只读 evdev 输入事件实现，需对键盘、鼠标设备有读取权限；插件不会自行修改系统权限。权限不足时显示提示，可在桌面用户终端执行（需要 `acl`、`udevadm` 和 `sudo`）：

```sh
bash scripts/enable-global-input.sh
```

这个脚本仅给当前用户添加当前设备的读取 ACL，允许同一用户运行的程序读取这些设备。插件会自动重试，无需注销；设备重新连接或重启后需重新授权。安装包附带同名脚本。撤销时先把动作按键设为“关闭”、鼠标移动改回“屏幕位置”，再执行 `bash scripts/enable-global-input.sh --revoke`。

例如 `p298` 模型的 W/A/S/D、空格播放五个动作，鼠标左右键按住表情，数字 1/2 切换表情。支持字母、数字、空格、方向键、常用修饰键、F1–F12 和鼠标左/中/右键；暂不包含 VTube Studio 的摄像头/人脸追踪映射、动画停在最后一帧、定时关闭或音频功能。

在真实桌面上可分别指定窗口系统和 Qt Quick 显示后端：

```sh
QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=vulkan \
  ./build/live2d-preview --render-backend vulkan /absolute/path/to/Haru/Haru.model3.json
```

这是预览窗口；桌面上的透明显示由 Plasma 小部件负责。

## 构建开发使用

```sh
cmake --install build
```

这是带 C++ 插件与渲染程序的小部件，单独安装 `package/` 不够。

| 内容 | 默认路径（相对安装前缀） |
| --- | --- |
| 小部件 | `share/plasma/plasmoids/org.kde.plasma.live2d` |
| QML 插件 | `lib/qt6/qml/org/kde/plasma/live2d` |
| 两种渲染程序 | `libexec/kde-live2d/live2d-render-{opengl,vulkan}` |
| 预览程序 | `bin/live2d-preview` |

若前缀为 `~/.local`，请让 Plasma 找到 QML 插件。在 `~/.config/plasma-workspace/env/live2d.sh` 中加入以下内容（先创建目录）：

```sh
#!/bin/sh
export QML_IMPORT_PATH="$HOME/.local/lib/qt6/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}"
```

想重载可以终端中执行 plasma 重启命令，重启完后即可生效

> 或者也可以直接注销重新登录，但有点麻烦

```sh
source "$HOME/.config/plasma-workspace/env/live2d.sh"
systemctl --user import-environment QML_IMPORT_PATH
systemctl --user restart plasma-plasmashell.service
```

后续更新插件后，只需再次执行最后一条重启命令。桌面和面板会短暂重新加载，已打开的应用窗口继续保留。C++ 插件更新需要重启 Plasma 进程，移除再添加小部件不能保证重新加载原生库。也可以选择注销并重新登录

部分发行版使用 `lib64`，以安装输出为准调整路径。可用 `-DLIVE2D_QML_INSTALL_DIR=...` 与 `-DLIVE2D_HELPER_INSTALL_DIR=...` 自定义安装目录；插件相对自身位置查找渲染程序，支持完整目录搬迁

重新加载桌面后，右键桌面 → **添加小部件** → **Live2D 桌面伙伴 / Live2D Companion**，在设置里选择模型和渲染后端。无需设置 `QSG_RHI_BACKEND`；如需测试 Plasma 本身的 Vulkan 后端，应按发行版的 Qt/Plasma 配置方式启动会话

开发时可在窗口内打开小部件：

```sh
QML_IMPORT_PATH="$PWD/build/qml" plasmawindowed "$PWD/package"
```

## 验证

默认 `ctest` 验证跟随策略、屏幕坐标、旧配置迁移、KWin 脚本的通知和清理逻辑，以及模型路径、中文文件名、无效 JSON、缺失文件和目录越界引用。脚本测试使用 Qt JavaScript 引擎和模拟的 KWin 对象，不连接真实桌面。提供模型后，会增加真实渲染与小部件生命周期测试：

```sh
cmake -S . -B build \
  -DLIVE2D_TEST_MODEL=/absolute/path/to/CubismSdkForNative-5-r.5/Samples/Resources/Haru/Haru.model3.json
cmake --build build -j
ctest --test-dir build --output-on-failure --timeout 60
```

OpenGL 使用无窗口 EGL。Vulkan 测试需要可用的 GPU 驱动或 lavapipe；可通过 `VK_DRIVER_FILES=/path/to/lvp_icd.json` 选择驱动。若安装了 Qt Gui 私有开发头文件且 Qt ≥ 6.6，还会增加 Qt Quick Vulkan 离屏显示测试；匹配版本的 Qt Gui 私有开发模块也用于生产路径的纹理复用

检查包括：真实 MOC3 与纹理、透明背景、预乘 alpha、连续动画、点击和表情、尺寸变化、重新加载、无限制帧率及切换、暂停/隐藏、失败恢复、自动回退与手动 Vulkan 失败，以及 VTube Studio 动作/表情引用的路径检查、第一帧部件透明度、聚焦输入、全局输入事件解码、组合键、表情释放和设备断开。鼠标映射还验证范围/反向/限幅、帧率无关平滑、独立屏幕坐标、与表情叠加、不重复叠加视线及关闭恢复。相对移动验证 evdev 分包、丢包复位、滚轮排除、订阅清理、灵敏度/边界和回中，以及屏幕指针不动时驱动真实模型渲染、按键关闭时独立运行、暂停/隐藏后复位。Qt Quick Vulkan 显示检查分别接收 OpenGL 和原生 Vulkan 生成的角色，保存 `build/presentation-opengl.png` 与 `build/presentation-vulkan.png`

检查 Vulkan 验证层：

```sh
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  ctest --test-dir build -V --timeout 60
```

已在 Qt 6.12、Cubism Native 5 R5、Mesa 软件 OpenGL 与 lavapipe 下通过十项测试，包括悬浮窗口的透明渲染、位置边界、关闭回调及创建/销毁。原生 Vulkan 渲染测试未报告验证层错误；本机 Qt Quick 的 Vulkan 显示测试仍报告 `PREINITIALIZED + OPTIMAL` 图像布局诊断，来源是已安装的 Qt QRhi，显示结果通过像素检查；Qt 上游已将该初始化改为 `UNDEFINED`，需要对应版本或发行版修复。实际 Plasma Wayland 会话中的置顶堆叠、拖动、设置持久化、全局跟随、全局按键、多显示器和硬件显卡仍需桌面环境验证；离屏测试不等同于已在真实 Wayland 会话中验收。

## 当前边界

- 默认显示在桌面小部件层，可开启“始终置顶”移到透明悬浮层；桌面与悬浮角色均支持可切换的鼠标穿透。
- 全局指针跟随依赖 KWin 的脚本接口；动作音频、嘴型同步暂未接入。
- 互动依赖模型的 `HitAreas`、动作组和表情定义。
- 已验证官方 Haru 和 VTube Studio 的 Hiyori、p298 模型；复杂混合模式及其他模型还需要兼容性覆盖。
- 两种后端均有 Mipmap 过滤；尚未启用 MSAA 或超采样，几何边缘的抗锯齿还可改善。

仓库代码沿用 [GPL-3.0](LICENSE) Cubism Core、Framework 和模型资产遵循各自授权条款，未包含在仓库中。分发构建产物时需保留第三方许可，并遵守 [Live2D SDK 授权要求](https://www.live2d.com/en/sdk/license/)