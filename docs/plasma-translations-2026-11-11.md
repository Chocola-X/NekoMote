# Plasma 翻译工作区核查

2026-11-11，基线 `df9a9c0`。用户要求核查另一Agent未提交的Plasma5/6翻译改动，
修正问题、验证后提交；原作者的harness/model未提供。本轮未部署或重启用户桌面。

## 修正的问题

- 原实现不检查locale，英文等桌面也会加载简体中文。现在按`QLocale`选择，当前提供
  `zh_CN`；未提供的语言保留英文源文案。繁体中文不是简体中文别名，当前同样保留英文。
- 原Plasma6在壁纸item构造时安装翻译，配置页单独打开可能仍是英文；Qt5还有重复加载器。
  统一在QML模块的`initializeEngine()`安装，配置页主动导入模块，创建任何壁纸实例前即就绪。
  Qt6保留生成的类型注册符号引用并使用非可选自定义插件，避免LTO裁剪或跳过初始化。
- 原`.ts`的两个长段落使用字面`\n`，与QML实际换行不匹配，导致中文提示查找失败。
  目录改成真实换行，Qt lupdate提取的全部11条消息与目录的context/source逐条匹配。
- 原`config`/`main`上下文会在同进程其他插件中碰撞。使用唯一的
  `NekoMoteWallpaperConfig`/`NekoMoteWallpaperMain`，资源路径也限定在本插件命名空间。
- translator由QCoreApplication持有；共享入口的QPointer防重复安装和应用销毁后的悬空引用。
  Plasma5/6共享同一份目录、资源模板与安装实现；不在帧路径读取locale、解析或加载翻译。
- 补齐CI的LinguistTools、设置页QML模块、显式测试目标及Arch构建依赖，避免本地通过而
  远端缺包或只注册测试却没有构建runner。Qt5/6及既有发行基线保持不变。

Qt宿主文案跟随桌面/应用locale，与壁纸内原生控制面板自己保存的语言偏好独立。
本轮不增加未提供的语言目录，也不增加运行中切换系统语言的业务接口。

## 验证

本机GCC15.2.1、Release/LTO/Werror，Qt6.9.2、Qt5.15.17：

| 检查 | 结果 |
| --- | --- |
| Qt6的en_US、zh_CN、zh_TW三个进程 | 3/3通过 |
| Qt5的en_US、zh_CN、zh_TW三个进程 | 3/3通过 |
| Qt6/Qt5原有staged module导入 | 2/2通过 |
| Qt5原有完整package/context/configuration导入 | 1/1通过 |
| lupdate目录核对、C++格式、shell语法、diff空白检查 | 通过 |

新增的`tests/plasma/test_translations.cpp`加载**实际stage设置页**，不先创建壁纸item，
核查标签语言、两段长提示的语言与真实换行、状态提示翻译、通用宿主context不受影响，
并在第二个QQmlEngine中再次加载同一设置页。六个新runner均无需素材或显示服务器。
测试不涉及模型求值、GPU公式或正式帧路径，未为该UI初始化变更测量播放性能。

本机缺Qt5 LinguistTools；Ubuntu官方`qttools5-dev`和`qttools5-dev-tools`
5.12.8-0ubuntu1仅解压到`/tmp/nekomote-i18n/qt5-tools`，通过显式
`Qt5LinguistTools_DIR`配置，`lrelease -version`为5.12.8；实际插件链接本机Qt5.15.17。
没有安装系统包。这验证了旧翻译工具与目录编译，不等于Ubuntu20全量构建或运行认证。
CI镜像定义及目标已更新，完整Debian12/Ubuntu20镜像、ARM64和远端Actions待后续运行。

复现入口（依赖安装完整时无需临时目录）：

```bash
cmake --preset plasma -DNEKOMOTE_BUILD_TESTS=ON -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build build/plasma --target nekomote_plasma_translation_test nekomote_plasma_module_test
ctest --test-dir build/plasma -R 'plasma_translations_|plasma_module_import' --output-on-failure

cmake --preset plasma5 -DNEKOMOTE_BUILD_TESTS=ON
cmake --build build/plasma5 --target nekomote_plasma_translation_test nekomote_plasma_module_test nekomote_plasma5_package_test
ctest --test-dir build/plasma5 -R 'plasma_translations_|plasma_module_import|plasma5_package_import' --output-on-failure
```
