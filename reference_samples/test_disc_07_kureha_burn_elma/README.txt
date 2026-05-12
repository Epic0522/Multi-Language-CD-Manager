七号吴叶刻录 Elma 对照盘抓取包

目的：
- 使用吴叶这个已经验证过能做出成功盘的刻录入口，
  去刻录六号所用的 Elma 曲目集合。
- 这张盘是一个高价值对照样本，用来区分：
  1. 是我们自己的烧录/重建链有问题
  2. 还是这组曲目内容本身会把 CD-TEXT 结构推向 W770J 不喜欢的形状

当前已抓到的证据：
- cdrdao-cdtext.txt：成功读出单日文 CD-TEXT
- cdrdao-read-toc.txt / sample-read.toc：成功读出 18 轨 TOC
- drutil-list.txt：成功
- drutil 高层重新抓取未完整保留，当前目录以 cdrdao 低层样本为主

当前最关键的 cdrdao 结果：
- TITLE = Ｅｌｍａ　＆　創作
- PERFORMER = ヨルシカ
- SIZE_INFO = {128, 1, 18, 0, 18, 16, ..., 36, ..., 105, ...}

和已有样本对比：
- 吴叶成功样本：
  - TITLE=18
  - PERFORMER=5
  - blockLast[0]=25
- 三号：
  - TITLE=17
  - PERFORMER=6
  - blockLast[0]=25
- 六号半成品：
  - TITLE=16
  - PERFORMER=6
  - blockLast[0]=24
- 七号这张：
  - TITLE=18
  - PERFORMER=16
  - blockLast[0]=36

当前判断：
- 这张盘最醒目的异常不是 TITLE，而是 PERFORMER 流异常膨胀。
- 如果吴叶这次确实带上了单曲艺术家，那么这张盘非常支持这样一个方向：
  单曲艺术家占位/填充方式会显著改变 PERFORMER 打包形态，
  并且这种变化的幅度足以远超三号和六号。
- 因此这张盘对“空单曲艺术家是否关键”这个问题非常有用，
  但它本身也不再是“吴叶成功样本原样复刻”，而是新的变量实验盘。
