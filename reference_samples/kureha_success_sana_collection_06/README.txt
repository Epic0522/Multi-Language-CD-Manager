KUREHA 成功样本盘抓取包（Sana Collection 06）

内容说明：
- drutil-list.txt: 系统看到的光驱列表
- drutil-status.txt: 介质/轨道总览
- drutil-toc.txt: 系统 TOC 输出
- drutil-cdtext.plist: drutil 读出的原始 CD-TEXT plist
- diskutil-info.txt: 盘面与挂载信息
- diskutil-unmount-before-cdrdao.txt: 运行 cdrdao 前的卸载日志
- cdrdao-cdtext.txt: cdrdao 视角下的 CD-TEXT 输出（含 SIZE_INFO）
- cdrdao-read-toc.txt: cdrdao read-toc 运行日志
- sample-read.toc: 若成功生成，则为 cdrdao 读出的 toc 文件
- cdtext-summary.json / txt: 从 drutil plist 提炼出的文本摘要

注意：
- 这是“吴叶刻录成功、车机可识别”的标准参考样本。
- 后续 cdtext-diff / pack 结构对照，应优先以这套样本为基线。

推荐命令：
- 直接按样本文本重建当前 CDManager 的 pack 报告：
  ./build/cdtext-diff export-current --sample-dir reference_samples/kureha_success_sana_collection_06 --json /tmp/kureha-sana-export.json
- 同时导出可直接喂给 `--format cdt` 的原始 pack blob：
  ./build/cdtext-diff export-current --sample-dir reference_samples/kureha_success_sana_collection_06 --raw-out /tmp/kureha-sana-export.bin
- 解析当前导出结果：
  ./build/cdtext-diff parse /tmp/kureha-sana-export.json --format packs-json
- 直接把样本目录当输入，对照当前导出：
  ./build/cdtext-diff compare reference_samples/kureha_success_sana_collection_06 /tmp/kureha-sana-export.json --left-format reference-sample --right-format packs-json
