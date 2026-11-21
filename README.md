# SA_APMM

《心灵终结》3.3.6 的独立局内 APM 插件。统计最近 60 秒内的游戏按键和鼠标点击，排除聊天、菜单和长按重复。新开对局或读档时清零。

APM 显示在左下角 `FPS / Avg` 右侧。关闭 FPS 时，APM 仍在左下角显示。

## 安装

把 `dist` 中的 `SA_APMM.dll` 和 `SA_APMM.ini` 放进 `gamemd.exe` 所在的游戏目录。插件由 Syringe 加载，使用 Ares 的状态栏绘制。安装后重新开一局即可生效。

可独立使用，也可与 SapphireA `1.0.741-selected.6` 及更新版一起使用。若使用内置 APM 的旧版 SapphireA，请先升级或关闭旧版的 APM。

## 开关

默认开启。将 `SA_APMM.ini` 的 `[MatchAPM]` 下的 `Enable` 改为 `0`，下次进入对局时关闭。

## 构建

使用 32 位 MinGW-w64，在本目录运行 `python build.py`。如需指定工具链，将 `MOE_MINGW` 设为工具链的 `bin` 目录。
