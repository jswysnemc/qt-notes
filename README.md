# qt-notes

基于 Qt 6 Widgets 的 Linux 桌面便签应用，为 Wayland / niri 的无边框分离窗口场景设计。

[English](README.en.md)

## 功能

### 窗口与交互

- 自定义无边框窗口，标题栏拖动通过 `startSystemMove()` 实现，适配 Wayland
- 多便签以独立窗口打开，各自维护窗口状态
- 启动时可配置默认打开最后关闭、最后编辑或最后创建的便签
- 标题栏按钮：便签列表、主题切换、设置、新建、关闭
- 双击标题栏可重命名便签

### 便签列表

- 支持多选、全选、批量删除
- 双击条目可重命名标题
- 左键在当前窗口切换便签，右键在独立窗口打开
- 默认按最后编辑时间排序，可在设置中改为创建时间或标题排序

### 加密

- 按便签启用加密，标题和正文以密文存入 SQLite
- 全局简单密码 + 全局复杂恢复密码双层机制
- 连续输错简单密码 3 次后，必须使用恢复密码解锁
- 加密便签的标题在列表和锁定状态下保留前半段明文，后半段以 `*` 模糊显示，便于辨认
- 含图片附件的便签暂不支持启用加密，防止附件明文落盘
- 加密方案：XChaCha20-Poly1305 + Argon2id 密钥派生，数据密钥可存入系统密钥环（libsecret）

### 编辑器

- 支持系统字体选择和最近使用字体列表
- 字体与字号为全局设置，修改后所有便签同步生效
- 支持自动换行开关
- 支持 `Ctrl + 滚轮` 调整字号
- 支持粘贴剪贴板图片和拖入本地图片文件
- 图片过大时自动缩放并限制存储体积，右键支持预览、复制、删除
- 单击图片或右键菜单可打开图片预览对话框，按原始大小显示并支持滚动查看
- 支持横向和纵向滚动

### 存储

- 自动保存文本、主题、字体、换行设置和窗口几何信息
- 设置中可删除当前便签，删除前二次确认

## 构建

依赖：Qt 6（Core、Gui、Widgets、Sql）、libsodium、libsecret

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/qt-notes
```

Arch Linux 安装依赖：

```bash
pacman -S qt6-base libsodium libsecret
```

## 数据位置

| 数据 | 路径 |
|------|------|
| 数据库 | `~/.local/share/snemc/qt-notes/notes.db` |
| 图片附件 | `~/.local/share/snemc/qt-notes/assets/` |
| 应用设置 | `~/.config/snemc/qt-notes.ini` |
| 解锁状态 | `~/.local/share/snemc/qt-notes/unlock/` |

## Wayland / niri 说明

- `qt-notes` 使用自定义无边框窗口，标题栏拖动通过 `startSystemMove()` 实现，兼容 Wayland
- 若希望便签默认以浮动窗口打开，可在 `niri` 中为 `qt-notes` 配置窗口规则；如需稳定的 `app-id` 匹配，建议安装 `qt-notes.desktop`
- 窗口大小恢复可靠
- 窗口位置行为因平台而异：
  - **X11**：应用保存并恢复窗口坐标与尺寸
  - **Wayland / niri**：应用保存尺寸和上次所在屏幕信息，精确坐标由 compositor 决定

## 许可证

MIT
