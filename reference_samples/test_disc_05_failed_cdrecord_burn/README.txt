五号测试碟抓取包（cdrecord RAW96 首次真烧失败样本）

当前已确认表现：
- 车机 W789J：不识别
- 车机 W770J：不识别
- PC（Windows 老软件）：未在本目录中补录，当前重点是盘结构证据

当前抓取结果：
- drutil 状态 / TOC 已抓取
- drutil cdtext 明确返回：No CD-Text information available on the disc
- cdrecord -vv -toc 已抓取
- cdrecord -vv -toc 明确返回：CD-Text len: 526
- diskutil 介质信息已抓取
- cdrdao read-toc 在这台 macOS + USB 光驱环境下仍无法正常 setup device

关键矛盾点：
- 高层读取视角（drutil）认为这张盘“没有 CD-Text”
- 较底层读取视角（cdrecord -toc）认为这张盘存在 526 字节 CD-Text
- 说明这张盘不是“完全没有写入任何 CD-Text”，而是“写进盘上的内容没有被高层读取链或车机正常识别”

当前判断：
- 这张盘比三号更差，不是单纯语言块偏好问题
- 更像是：盘上存在某种 CD-Text 数据，但其结构/内容/映射没有达到吴叶成功样本那种可识别状态
- 尤其需要结合本次烧录前 UI 异常一起看：
  - Album Title 为空
  - Album Artist 串入了标题与其他文本
  - 轨道元数据装配在进入真正烧录前就已有错位迹象

注意：
- 该目录不是标准 reference-sample；`cdtext-summary.json` 仅作为失败记录占位，不应用作正常对比基线
- 后续如果要继续追这条线，应优先回查“项目字段 -> preparation -> write payload -> pack assembly”的内容装配问题
