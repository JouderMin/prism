# third_party

本目录只作 **JUCE 兜底** 用，正常构建不需要它。

## 首选：vcpkg 的 JUCE 8.0.7

```sh
vcpkg install juce --triplet arm64-osx
```

装好后 `~/vcpkg/installed/arm64-osx/share/juce/JUCEConfig.cmake` 存在，
`CMakePresets.json` 里的 vcpkg toolchain 会让 `find_package(JUCE CONFIG)` 找到它。

## 兜底：JUCE 子模块

spec 8.1 / R1：若 vcpkg 的 JUCE 8.0.7 与本机 Xcode 27 冲突（编译或链接失败），
改用官方源码树，`CMakeLists.txt` 其余内容不变。

```sh
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
git -C third_party/JUCE checkout 8.0.7
```

`CMakeLists.txt` 的查找顺序是：

1. `find_package(JUCE CONFIG QUIET)`——vcpkg 的包；
2. 找不到且 `third_party/JUCE/CMakeLists.txt` 存在时 `add_subdirectory(third_party/JUCE)`；
3. 两者都没有则 `FATAL_ERROR`，提示先装 vcpkg 包或放子模块。

## 约束

- 版本固定 **8.0.7**，与 vcpkg port 一致；不要用其他版本混搭。
- 该目录内容（含子模块）不应被复制进插件产物。
- 恢复首选路径：删掉 `third_party/JUCE`，重新 `cmake --preset release` 即可。
