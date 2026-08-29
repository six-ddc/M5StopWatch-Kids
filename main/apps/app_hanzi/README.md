# AppHanzi — 汉字笔顺

给小朋友的识字笔顺 App：在田字格里逐笔写出一个字，配拼音，按小学语文**写字表**的课文顺序编排。

- 字库：一年级上册 ~ 三年级上册写字表累计 **1037 字 / 132 课**，平均 8.0 画
- 渲染：自绘扫描线光栅化 + 圆头笔刷沿骨架中线推进，**不依赖 ThorVG / 矢量图形**
- 体积：笔顺数据 611 KB + 中文子集字体 ~100 KB + 图标 78 KB，全部编进 `.rodata`

## 操作

| | 浏览页（课文字格） | 学习页（田字格） |
|---|---|---|
| A 键 | 上一页/上一课 | 上一个字 |
| B 键 | 下一页/下一课 | 下一个字 |
| 点击 | 选字 → 学习页 | 重播动画 |
| A+B 长按 | 退出 App | 返回浏览页（再按退出）|

进度（最后学到的字）存在 NVS 命名空间 `hanzi` 下。

## 代码结构

```
engine/       与 LVGL、ESP-IDF 无关，可在主机编译和逐像素验证
  hz_data     HZS1 二进制解码：索引查找、贝塞尔展平到屏幕坐标
  hz_raster   扫描线 even-odd 填充，4× 垂直子采样 + 水平分数覆盖 → A8
  hz_anim     播放时序状态机（纯时间驱动，不碰像素）
  hz_compose  图层合成：base / stroke / reveal 三个覆盖度平面 + RGB565 输出
view/         LVGL 页面，PSRAM 缓冲的所有权在这一层
```

合成规则：可见墨迹 = `max(base, min(stroke, reveal))`。三层都是同一种墨色，用 `max` 而非 alpha-over 合并，避免抗锯齿边缘在图层重叠处被加深。

### 几个必须遵守的约束

- **解码器逐字节读取**：笔画数据是字节紧凑打包的，int16 坐标会落在奇数地址；Xtensa 对非对齐 32 位访问会抛 `LoadStoreAlignmentError`。
- **`heap_caps_malloc` 不清零**：所有缓冲分配后必须 `memset`，否则未绘制像素在真机上是随机噪点（主机模拟器抓不到这类问题，静态内存被 loader 清零了）。
- **每帧只发一个失效矩形**：LVGL 里每个失效区域都是一次完整渲染 pass。
- **NVS 写入不能持 LVGL 锁**，也不能在绘制回调里：写 flash 会关 cache 并挂起另一个核。
- `LearnPage::update()` 必须在持有 LVGL 锁时调用（它直接写活动的 canvas 缓冲）。

## 数据管线

```bash
python3 tools/hanzi_pipeline/build_hanzi_data.py     # 生成笔顺数据 + 字体字符集 + golden 参考图
python3 tools/hanzi_pipeline/make_icon.py            # 生成 launcher 图标
```

管线做完了所有规范化，固件侧不重复劳动：y 轴翻转（源数据 y 向上、基线 900）、坐标量化到 512 系、每笔预算笔刷半径、填充规则一致性校验、体积门禁（>700 KB 构建失败）。

字体子集由管线导出的字符集生成（**不要手工维护字符集**，漏字会静默显示成 □）：

```bash
npx lv_font_conv --font LXGWWenKai-Regular.ttf --size 24 --bpp 4 --format lvgl \
  --no-compress --lv-include lvgl.h --symbols="$(tr -d '\n' < main/assets/fonts/charset_hanzi_ui.txt)" \
  -o main/assets/fonts/lv_font_hanzi_ui_24.c
```

## 主机验证

引擎不依赖设备，改动后应先在主机跑完这两个：

```bash
cmake -S tools/hanzi_host_test -B build_host && cmake --build build_host
./build_host/hanzi_host_test                   # 1037 字逐字与 golden 比对
./build_host/anim_host_test --char 6211        # 动画 + 增量绘制 vs 全量重绘的残影检测
```

`anim_host_test` 每帧都会把增量绘制的结果与一次全量重绘做逐像素比对，必须完全一致——脏区算错、图层残留这类 bug 会在这里当场暴露。

## 数据来源

- **笔顺数据**：[hanzi-writer-data](https://github.com/chanind/hanzi-writer) → 派生自 [Make Me a Hanzi](https://github.com/skishore/makemeahanzi)，其字形数据来自 **Arphic PL 字体**，依 **Arphic Public License** 授权，要求保留本声明。
- **字表与拼音**：[vipzhicheng/shukong-app](https://github.com/vipzhicheng/shukong-app) 整理的小学语文教材数据（MIT）。
- **界面字体**：[霞鹜文楷 LXGW WenKai](https://github.com/lxgw/LxgwWenKai)，**SIL Open Font License 1.1**。选楷体是因为小学课本用楷体，且与笔顺数据的字形风格一致。

前两项在代码里有默认端点，直接跑就能拉；要换源就覆盖环境变量：

| 变量 | 指向 | 什么时候要 |
| --- | --- | --- |
| `$STROKE_URL` | 单字笔顺 JSON 的 URL 模板（一个 `{}` 占位符放字符） | `.cache/strokes/` 没缓存时 |
| `$BOOK_URL` | 分册字表 JSON 的 URL 模板（占位符放册号） | `.cache/books/` 没缓存时 |
| `$UI_FONT` | 生成子集字体用的 TTF | 字体不在 `.cache/fonts/` 时 |

笔顺 JSON 要有一个 `strokes` 数组，装 SVG 路径，坐标在 1024 单位的框里、基线 y=900；字表 JSON 每课要有 `recognition`（识字表）和 `writing`（写字表）两个字段，各自带逐字拼音。满足这两个形状的数据源都能直接接。

`.cache/` 暖起来之后就都不需要了。上述声明同时保留在生成物 `main/assets/hanzi/hanzi_data.c` 的文件头中。
