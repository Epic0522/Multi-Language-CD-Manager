# KUREHA / ZENKI CD-TEXT Reverse Engineering Notes

这份笔记只记录目前已经从 `KUREHA.EXE` / `ZENKI.DLL` / `MOMIJI.DLL`
静态分析里基本坐实、并且能直接指导 `CDManager` 继续实现的结论。

## 1. 三层分工

- `KUREHA.EXE`
  - VB6 前端
  - 暴露出 `objTrackTextEnglish`、`objTrackTextJapanese`
  - 暴露出 `chkTitle`、`chkPerformer`、`chkSongwriter`、`chkComposer`、
    `chkArranger`、`chkMessage`
- `ZENKI.DLL`
  - 轨道与文本模型层
  - 导出 `EnabledTrackText`、`SetTrackText`、`GetTrackText`
- `MOMIJI.DLL`
  - 设备与写盘层
  - 导出 `WriteStart`、`WriteLBA`、`WriteEnd`、`CheckWriteMode`

## 2. 六字段模型

从 `KUREHA.EXE` 的字符串可以确认，吴叶前端至少把 CD-TEXT 文本拆成六类：

1. `Title`
2. `Performer`
3. `Songwriter`
4. `Composer`
5. `Arranger`
6. `Message`

并且每个字段都有单独的启用开关：

- `TitleEnabled`
- `PerformerEnabled`
- `SongwriterEnabled`
- `ComposerEnabled`
- `ArrangerEnabled`
- `MessageEnabled`

## 3. 双语言文本银行

`KUREHA.EXE` 里直接有：

- `objTrackTextEnglish`
- `objTrackTextJapanese`

说明吴叶内部不是“一个文本对象 + 一个语言切换”，而是至少维护两套文本银行。

## 4. 专辑级 / 轨道级分层

`ZENKI.DLL` 的 `GetTrackText` 内部函数 `0x10001220` 表现为：

- `trackNo <= 0` 时走全局对象
- `trackNo > 0` 时走每轨对象

目前已定位到两个全局文本银行基址：

- `this + 0x1a0`
- `this + 0x234`

这两个对象会被单独清空，极像：

- 专辑级 English bank
- 专辑级 Japanese bank

## 5. 轨道对象大小

`ZENKI.DLL` 内部按轨索引的步长经过计算后为 `0xB0` 字节。

也就是说，轨道对象看起来是一个固定长度结构：

- `trackRecordSize = 0xB0`

## 6. 单个文本银行结构

内部函数 `0x10002f80` 的行为非常清楚：

- 参数 1：语言槽位
  - `0` -> 当前对象基址 `+0x04`
  - `1` -> 当前对象基址 `+0x4c`
- 参数 2：字段号 `0..5`
- 然后按字段号跳到六个固定偏移之一：
  - `+0x00`
  - `+0x0c`
  - `+0x18`
  - `+0x24`
  - `+0x30`
  - `+0x3c`

这说明：

- 每个语言槽位里正好有六个字段位
- 每个字段位像是一个固定大小的字符串/描述对象
- 一个语言槽位总长 `0x48`
- 两个语言槽位总长 `0x90`

## 7. 目前最像的 API 语义

虽然导出包装层还没完全恢复成源码级签名，但根据：

- `SetTrackText -> 0x10001110`
- `GetTrackText -> 0x10001220`
- `0x10002f80`
- `0x10003070`

目前最像的模型是：

```text
SetTrackText(handle, trackNo, languageSlot, fieldId, text)
GetTrackText(handle, trackNo, languageSlot, fieldId, outText)
```

其中：

- `trackNo <= 0` 代表专辑级
- `trackNo > 0` 代表轨道级
- `languageSlot` 至少有 `0` 和 `1`
- `fieldId` 在 `0..5`

### 7.1 已确认的内部入口

- `EnabledTrackText` 导出地址：`0x10011f00`
  - 实际只是在对象偏移 `0x19c` 写入一个 `0/1` 字节
- `SetTrackText` 导出地址：`0x10011f20`
  - 包装字符串对象后转调内部函数 `0x10001110`
- `GetTrackText` 导出地址：`0x10011f50`
  - 实际转调内部函数 `0x10001220`

### 7.2 GetTrackText 的层级路由

`0x10001220` 的行为已经比较清楚：

- `trackNo > 0`
  - 走每轨对象
- `trackNo == 0`
  - 走全局对象 `this + 0x234`
- `trackNo < 0`
  - 走全局对象 `this + 0x1a0`

这说明吴叶内部很可能不仅区分“轨道级 / 专辑级”，还区分两套专辑级文本银行。

### 7.3 文本银行字段访问器

`0x10002f80` 明确表现为“语言槽位 + 字段号 -> 字段对象”的访问器：

- `languageSlot == 0`
  - 基址偏移 `+0x04`
- `languageSlot == 1`
  - 基址偏移 `+0x4c`

字段号 `fieldId 0..5` 对应的槽位偏移为：

- `0 -> +0x00`
- `1 -> +0x0c`
- `2 -> +0x18`
- `3 -> +0x24`
- `4 -> +0x30`
- `5 -> +0x3c`

这再次支持“每个语言槽位里正好有六个字段对象”的判断。

### 7.4 写入字段字符串的核心函数

`0x10003070` 已经可以视作“把字符串写进指定语言槽位和字段槽位”的核心入口：

- 参数上看，至少会接收：
  - 目标文本银行对象
  - 语言槽位
  - 字段号
  - 源字符串对象
- 逻辑上：
  - 先按 `languageSlot` 选 `+0x04` 或 `+0x4c`
  - 再按 `fieldId` 选六个字段槽位之一
  - 最后调用 `0x100058e0`

而 `0x100058e0` 的行为非常清楚：

- 从源字符串对象的 `+0x04` 取底层 C 风格字符串
- 分配同等长度的新缓冲区
- 逐字节复制
- 写入目标字段对象的 `+0x04`

也就是说，吴叶内部的文本字段对象本身就是一个“小型字符串持有者”，
而不是复杂容器。

### 7.5 清空 / 初始化文本银行

`0x10003190` 会把一个双语言六字段文本银行完整清空：

- 先清 language slot 0 的六个字段
- 再清 language slot 1 的六个字段

它调用的 `0x10005950` 会：

- 释放旧字符串
- 分配 1 字节新缓冲区
- 填入 `NUL`

也就是：字段默认并不是“未分配”，而是“已分配的空字符串”。

## 8. 组包描述表的雏形

`ZENKI.DLL` 里还有一组非常像“CD-TEXT 组包描述表”的函数：

- `0x10003140`
  - 返回对象偏移 `+0x04` 的 `short` 计数值
- `0x10003150`
  - 按索引复制一条固定长度记录

### 8.1 记录大小

`0x10003150` 的索引计算为：

- `index * 0x12`
- 再从 `base + 0x06` 开始取

并复制：

- `DWORD x4`
- `WORD x1`

合计：

- `0x12` 字节

这说明某个对象内部维护了一张：

- `count` 在偏移 `+0x04`
- `entries` 从偏移 `+0x06` 开始
- 每条 `entry` 长 `0x12`

的描述表。

### 8.2 为什么怀疑它和 CD-TEXT 组包有关

在 `KUREHA.EXE` 的类型/属性字符串里已经能看到：

- `CDTextAdd`
- `CDTextCount`
- `CDTextDelete`
- `bytPacketType`
- `bytPacketNo`
- `bytFlags`
- `bytCrc`
- `TOCCDText`
- `PacketType`
- `PacketNo`
- `Data`
- `Crc`

而 `0x10002133` 一带的代码会：

- 先取一张描述表的 `count`
- 循环索引
- 调 `0x10003150` 把每条 `0x12` 记录拷出
- 再把记录喂给后续对象处理

这非常像：

- 内部先生成一张“CD-TEXT 描述记录表”
- 再逐条转成前端可见的 `TOCCDText / CDTextAdd` 项

目前还没有把这条链完整追到 `CDTextAdd` 最终调用点，但已经很接近“组包前的中间表示”。

## 9. 对 CDManager 的直接启发

这意味着 `CDManager` 后面不该继续只围绕“一个项目标题 + 一组 TOC 文本”打补丁，
而应该逐步改成更像吴叶的内部模型：

- album-level text bank
  - English
  - Japanese
- per-track text bank
  - English
  - Japanese
- six field ids
  - title / performer / songwriter / composer / arranger / message
- per-field enabled flags

这样后面无论输出给：

- `cdrdao`
- 自组 raw pack
- 甚至更底层的 MMC 写入

都不会再只是临时拼接。
