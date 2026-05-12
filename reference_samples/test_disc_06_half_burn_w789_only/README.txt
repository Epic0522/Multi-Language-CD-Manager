六号半成品盘抓取包

当前已确认表现：
- 车机 W789J：成功识别日文
- 车机 W770J：不识别
- Mac 本机：Track 1 标题已恢复正常，不再是乱码，显示为「歩く」

这张盘的重要意义：
- 它证明「T1 乱码」不是 W770J 不识别的唯一原因。
- 相比三号 / 四号，这张盘已经修复了最醒目的第一轨标题损坏问题。
- 但在 T1 正常以后，W770J 仍然拒绝识别，说明还有更深一层的结构差异存在。

当前抓取到的证据：
- cdrdao-cdtext.txt：成功读出单日文 block 的 CD-TEXT
- cdrdao-read-toc.txt / sample-read.toc：读出了 18 轨 TOC
- drutil-status.txt / drutil-toc.txt：保留了系统侧轨道与介质信息
- drutil-cdtext.plist：本次未成功读出高层 CD-TEXT，返回 busy / no audio CD present

当前最关键的 cdrdao 结果：
- TITLE = Ｅｌｍａ　＆　創作
- PERFORMER = ヨルシカ
- SIZE_INFO = {128, 1, 18, 0, 16, 6, ..., 24, ..., 105, ...}

和其他盘的相对位置：
- 比五号更好：至少已经重新回到「日文 CD-TEXT 可读」状态，且 W789J 可以识别
- 比三号更接近根因：因为 T1 已经不乱码，但 W770J 仍然不认
- 仍未达到吴叶成功样本：成功样本是 TITLE=18, PERFORMER=5, blockLast[0]=25；这张盘是 TITLE=16, PERFORMER=6, blockLast[0]=24

当前判断：
- 这张盘是一个非常关键的中间态样本。
- 它说明修复 T1 后，问题已经被进一步收缩到更深层的 pack / SIZE_INFO / block 结构差异。
