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

| 项目 | 支持版本                    | 说明   |
| :--- | :---                        | :---   |
| KDE  | KDE Plasma 6.8+ 及以上      |        |
| QT   | Qt 6.5+                     |        |
| 架构 | x86_64                      | Arm好像支持，但由于没有设备无法测试 |
| 显示服务器 | 仅 Wayland            | 不支持 x11，因为 KDE 也不支持了 |
| 图形后端 | Vulkan 1.3+ / OpenGL EGL |       |
| 支持格式 | 支持 `.model3.json` 模型 | 不支持 Cubism 2 的 `.model.json` |

# 安装部件

在 GitHub **Actions → Build plugin → 成功的运行 → Artifacts** 下载 `kde-widget-live2d-archlinux-x86_64`，解开下载的 ZIP，再解开里面的 `.tar.gz`，进入目录执行：

```sh
bash install.sh
```

默认安装到 `~/.local`。按照脚本输出的命令重启 Plasma 桌面后，在“添加小部件”中选择 **Live2D 桌面伙伴**。这是包含 C++ 原生插件和渲染程序的安装包，不能直接用“从本地文件安装小部件”导入。可执行 `bash install.sh /absolute/install/prefix` 指定安装目录。

自动构建的二进制面向最新 Arch Linux x86_64，需要系统 Qt、Plasma 和显卡驱动；其他发行版或较旧 Qt 环境建议从源码构建。包内的 `BUILD-INFO.txt` 记录构建环境版本。不包含 SDK 源码或示例模型；使用时选择自己的 `.model3.json`。

# 预览

做完先

# 构建 

> 以下Ai写的

需要 CMake 3.22+、C++17、Qt 6 Quick / QML / Quick Controls / Quick Dialogs / DBus、GLEW、OpenGL / EGL、Vulkan 头文件与 loader、`glslc`。测试需要 Qt Test；真实模型测试需要 Python 3.9+。运行小部件还需要 Plasma 6 和 Kirigami。

Arch Linux 开发依赖：

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-declarative glew libplasma kirigami vulkan-headers vulkan-icd-loader shaderc
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

OpenGL 使用无窗口 EGL。Vulkan 测试需要可用的 GPU 驱动或 lavapipe；可通过 `VK_DRIVER_FILES=/path/to/lvp_icd.json` 选择驱动。若安装了 Qt Gui 私有开发头文件且 Qt ≥ 6.6，还会增加 Qt Quick Vulkan 离屏显示测试；私有 Qt API 仅在测试中使用

检查包括：真实 MOC3 与纹理、透明背景、预乘 alpha、连续动画、点击和表情、尺寸变化、重新加载、暂停/隐藏、失败恢复、自动回退与手动 Vulkan 失败。Qt Quick Vulkan 显示检查分别接收 OpenGL 和原生 Vulkan 生成的角色，保存 `build/presentation-opengl.png` 与 `build/presentation-vulkan.png`

检查 Vulkan 验证层：

```sh
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  ctest --test-dir build -V --timeout 60
```

已在 Qt 6.12、Cubism Native 5 R5、Mesa 软件 OpenGL 与 lavapipe 下通过七项测试。原生 Vulkan 渲染测试未报告验证层错误；本机 Qt Quick 的 Vulkan 显示测试仍报告 `PREINITIALIZED + OPTIMAL` 图像布局诊断，来源是已安装的 Qt QRhi，显示结果通过像素检查；Qt 上游已将该初始化改为 `UNDEFINED`，需要对应版本或发行版修复。实际 Plasma Wayland 会话中的拖动、设置持久化、全局跟随、多显示器和硬件显卡仍需桌面环境验证；离屏测试不等同于已在真实 Wayland 会话中验收。

## 当前边界

- 角色属于桌面小部件层。窗口置顶、跨应用悬浮、鼠标穿透暂未实现。
- 全局指针跟随依赖 KWin 的脚本接口；动作音频、嘴型同步暂未接入。
- 互动依赖模型的 `HitAreas`、动作组和表情定义。
- 只验证了官方 Haru 模型；复杂混合模式及其他模型还需要兼容性覆盖。
- Vulkan 纹理目前使用单级线性采样；缩小角色时的抗锯齿与性能还可优化。

仓库代码沿用 [GPL-3.0](LICENSE) Cubism Core、Framework 和模型资产遵循各自授权条款，未包含在仓库中。分发构建产物时需保留第三方许可，并遵守 [Live2D SDK 授权要求](https://www.live2d.com/en/sdk/license/)
