四号测试碟抓取包

当前已确认表现：
- 车机 W789J：识别日文
- 车机 W770J：识别英文并显示为空
- PC（Windows 老软件）：识别为正确轨道数，但有文本错乱与顺延问题

当前抓取结果：
- drutil 侧 CD-TEXT 已成功抓取
- drutil plist 中检测到两个 language block
- block 0: language=en，标题/表演者均为空格，占位为英文空块
- block 1: language=ja，包含真实日文专辑与轨道文本
- cdrdao cdtext 已成功抓取，确认存在两组 TITLE/PERFORMER/SIZE_INFO

关键结构特征：
- 英文块 SIZE_INFO: { 0, 1, 18, 0, 4, 4, ..., 3, 10, 25, ..., 9, 105, ... }
- 日文块 SIZE_INFO: {128, 1, 18, 0, 17, 6, ..., 3, 10, 25, ..., 9, 105, ... }
- 当前高层 reference-sample 摘要仅反映首个英文空块，因此会低估真实多语言结构

注意：
- 该目录同时包含高层 drutil 视角与较底层的 cdrdao cdtext 视角
- 解释 W789J / W770J / PC 分歧时，应优先结合 cdrdao-cdtext.txt 与 drutil-cdtext.plist 的双 block 结果一起看
