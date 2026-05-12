三号测试碟抓取包

当前已确认表现：
- 车机 W789J：识别日文
- 车机 W770J：不识别
- PC（Windows 老软件）：可识别

当前抓取状态：
- drutil 侧 CD-TEXT 已成功抓取
- diskutil / drutil 侧轨道与介质信息已抓取
- cdrdao read-toc 可读出 18 轨 TOC，但当前无法从这台 USB 光驱稳定读出更深层的 TOC / CD-TEXT 结构

注意：
- 该目录当前更偏“高层文本视角样本”，不是完整 raw lead-in 抓取包
- 用它和吴叶成功样本做 `reference-sample` 对比时，`schema` 可能仍然一致，因此不足以单独解释 W770J 不识别的根因
