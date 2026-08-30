# AppHanzi — 汉字笔顺

给小朋友的识字笔顺 App：在田字格里逐笔写出一个字，配拼音。入口是**拼音查字**（孩子通常"知道音、不知道写法"），教材课文浏览是第二入口。

- 字库：教材字（一上~三上识字表+写字表）**1847 字 / 181 课** + 笔画数据全集补齐到 **9562 字**（收录条件：有笔画数据 **且** 有可用拼音）。扩充字不属于任何课，只能通过拼音检索到达——order 表里教材字按教学序在前、扩充字按字频降序在后，"候选按常用度排序"就是 order 升序，不需要额外的权重表。扩充段内部再分两层：《通用规范汉字表》内的简体字在前，表外的繁体/异体/生僻字整体垫底——wordfreq 会把繁体折算到简体词频上，不分层的话"謹"会紧挨着"谨"出现在每个候选列表里
- 检索：**三种拼音输入模式，屏幕大范围横滑循环切换**（`main/apps/common/pinyin_ime/`，全部为可复用组件），左滑下一个、右滑上一个，选择存 NVS，下次打开还是孩子用惯的那个：
  1. **滚筒拼读选择器**（默认）——iOS 时间选择器范式：声母滚筒｜后缀滚筒｜汉字滚筒，一条横贯全屏的选中带把三列读成一个拼读公式 `h ｜ ao ｜ 好(hǎo)`。没有空态：三列任何时刻都拨在一个合法音节上且至少有一个字（枚举是数据驱动的，见 t9_engine）。列 1 是所有音节的首单元（zh/ch/sh 整体、a/o/e 等零声母元音也算，恰好 26 项）；列 2 是该声母下的合法后缀（不叫"韵母"——xian 的 ian 含介母，别跟课堂三拼术语打架），声母自成音节时列 2 出现一个安静的 `·`（a ｜ · ｜ 啊）；列 3 是整音节的全部候选按常用度滚动，多音字候选注音显示与拨出音节匹配的那个读音
  2. **26 字母拨盘**——字母沿表圈排一圈，逐字母精确输入：走不通的字母变暗且不吃触点（触点吸附到最近的亮字母，目标随音节推进越变越大），按住沿环滑动有放大镜跟随、跨字母短震、松手落定；中心是 4+4 候选格——每格下方标注带调读音，格宽是被最宽的 "chuáng"（20 px 调号字体下 70 px 墨迹）反推出来的：行半宽超过 157 格角就会越过环形触摸带内界（r=170）抢走字母触点，所以宽格只放得下四列，一页 8 个候选换来读音永不换行/截断。拼音没有双字母，所以"再点一次刚输入的字母 = 删掉它"。环上没有 v——拼音音节不含 ASCII v，这个槽位显示 **ü**：绿=lü、路=lu 是分开的两个前缀，跟拼音教学一致（引擎内部用 'v' 承载 ü，标准输入法惯例，见 `py_normalize.h`；所有上屏处经 `pyDisplay` 恒显示为 ü，孩子永远看不到 v）
  3. **T9 九宫格**——手机键盘布局的 8 个字母键 + 删除，上方是拼音解释 chips（默认选中最优解释，孩子可以完全无视）+ 候选条；按下去无路可走的键当场拒绝（短震 + 红框），不存在"输错进死胡同"的状态。8 键键帽是 **tuü** 不是 tuv——ü 的内部载体 'v' 恰好就在这个键上，数字路由零改动

  **切换携带当前输入状态**：载体 = 无调字母前缀（ü 以内部字母 v 承载）+ 当前选中候选的 id。滚筒拨到 xiang(想) 左滑，拨盘的前缀就是 xiang、候选格翻到"想"那页；再滑进 T9，数字串 94264、选中解释 xiang。反向也成立：拨盘输了一半的 "xi" 进滚筒会补全为该前缀下字母序最小的合法音节（xi 本身）；从拨盘/T9 的空态切走则各自落回默认（滚筒 = "hao"）。切换动画是两页按滑动方向的平移（220ms ease-out，纯 translate，不触发 LVGL 变换图层）；滚筒竖滑、拨盘环带按压、T9 键按压期间横滑被抑制——正在输入的那根手指永远赢
- 渲染：自绘扫描线光栅化 + 圆头笔刷沿骨架中线推进，**不依赖 ThorVG / 矢量图形**；候选字缩略图直接用笔顺引擎画（A8 + recolor），9562 个字形不进字体
- 体积：笔顺数据 7.9 MB 走 `EMBED_FILES` 链接（C 数组形式约 47 MB，GCC 每次要嚼几分钟）+ 中文子集字体 ~1.4 MB + 图标 78 KB

## 操作

| | 搜索页（默认落地） | 浏览页（课文字格） | 学习页（田字格） |
|---|---|---|---|
| A 键 | 滚筒：汉字滚筒上拨一格；拨盘/T9：候选上一页 | 上一页/上一课 | 教材进入：上一个字；搜索进入：返回搜索 |
| B 键 | 滚筒：汉字滚筒下拨一格；拨盘/T9：候选下一页 | 下一页/下一课 | 教材进入：下一个字；搜索进入：重写一遍 |
| 点击 | 滚筒：选中带 → 学习页、非选中行 → 滚到选中位；拨盘：环上输入字母、点候选 → 学习页；T9：按键输入、点候选 → 学习页 | 选字 → 学习页 | 重播动画 |
| 竖滑 | 滚筒：拨对应那列（惯性 + 行吸附，跨行短震） | — | — |
| 横滑 | **循环切换输入模式**（左滑下一个、右滑上一个，输入状态随行；正在拨轮/按环/按键时不切） | → 搜索页 | — |
| A+B 长按 | 退出 App | 返回搜索页 | 返回来处（搜索或浏览，再按退出）|

搜索交互为 5-8 岁设计：滚筒范式没有"没有结果"的状态可进——三列组合恒为合法音节；联动按 iOS 日期选择器语义：换声母时后缀若仍合法就保留（h+ao → g+ao），否则吸附字母序最近项，后缀变化则汉字列回到常用度第一位，被联动重建的列以 140ms 短促淡入进场而不是生硬跳变；多音字全部展开进索引（好 = hǎo/hào 都能查到）。触点落在哪列只滚哪列；轻点非选中行等于把它滚进选中带（防误触）。从学习页返回时三列位置原样保留（孩子常连看同音字），开机则用 NVS 里上次学的字反解音节初始化——打开搜索就是接着上次，无记录时默认"好"。

进度（最后学到的字 `last`）与输入模式（`imode`）存在 NVS 命名空间 `hanzi` 下。模式在手势回调里只置脏标记，真正的 NVS 写在 `onRunning()` 锁外完成（写 flash 会关 cache）。

## 代码结构

```
engine/       与 LVGL、ESP-IDF 无关，可在主机编译和逐像素验证
  hz_data     HZS1 二进制解码：索引查找、贝塞尔展平到屏幕坐标
  hz_raster   扫描线 even-odd 填充，4× 垂直子采样 + 水平分数覆盖 → A8
  hz_anim     播放时序状态机（纯时间驱动，不碰像素）
  hz_compose  图层合成：base / stroke / reveal 三个覆盖度平面 + RGB565 输出
view/         LVGL 页面，PSRAM 缓冲的所有权在这一层
  search      SearchPage：三输入模式的宿主——三个组件常驻 setHidden 切换，
              横滑手势路由 + 状态载体转运 + 平移切换动画；对 app 层的 API
              不随模式变（takePick / 候选翻页 / showCharacter 内部路由）

../common/pinyin_ime/   可复用拼音输入组件（不含任何 hz 代码，组件间互不 include）
  py_normalize   带调 UTF-8 → 无调 ASCII，表驱动，纯逻辑
  t9_engine      音节表引擎：unitAt/suffixAt 枚举"首单元+后缀"分解（每个音节
                 可且仅可分解一次，滚筒组合恒合法）、queryExact(音节)→字；
                 历史 T9 层 interpretations(数字串)/query(前缀) 保留为具体方法
                 供拨盘与 T9 键盘用；从通用 (音节, id) 表构建，id 升序即权重
                 降序，纯逻辑
  glyph_painter  GlyphPainter 抽象（往 A8 buffer 画候选 + 注音），三组件共用
  picker_view    LVGL 三列滚筒（圆柱投影鱼眼 + 惯性吸附 + 联动重建）；只认
                 CandidateSource（枚举/查询）与 GlyphPainter 两个抽象
  dial_view      LVGL 26 字母拨盘（角度命中 + 吸附 + 放大镜 + 回飞删除动画）
  ime_view       LVGL T9 九宫格（解释 chips 双端窗口 + 候选条 + 拒绝反馈）
```

拼音索引在 `AppHanzi::onOpen()` 运行期构建（遍历 `pinyinAt()`，多读音按空格切分归一化，11593 条读音实测 203 ms、索引 ~40 KB），blob 格式因此零改动。拼音字段存全部读音（"hǎo hào"，首个为主读音），学习页默认显示主读音；从搜索选中的候选会把点选时命中的读音透传过来显示。

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
python3 tools/hanzi_pipeline/make_char_table.py --source characters.csv
                                                     # 仅重生成 charlist.py 时用（见下）
```

管线做完了所有规范化，固件侧不重复劳动：y 轴翻转（源数据 y 向上、基线 900）、坐标量化到 512 系、每笔预算笔刷半径、填充规则一致性校验、体积门禁（>8192 KiB 构建失败）、拼音字符 ⊆ 字体子集断言、多音字哨兵断言（"好"必须带两个读音）。

字列表 = 教材字（教学序，带课程）+ `charlist.py` 里教材之外的字（按字频降序追加、不属于任何课）。`charlist.py` 是**进库的数据文件**（笔画数据全集 9562 字 + 多读音 + 分层频序），由 `make_char_table.py` 一次性生成、人工复核 diff 后入库；日常构建不需要它的依赖（pypinyin / wordfreq）。入库条件是"有笔画 且 有可用拼音"——全集里只有 龶龹龺 三个部件字因无读音出局，部首区符号（U+2E80..）也被剔除。教材字缺笔顺数据会硬失败，扩充字缺失只 skip 并打印清单。

blob 产物是 `main/assets/hanzi/hanzi_data.bin`，经 `EMBED_FILES` 链接进固件（主机测试用 `HANZI_BLOB_PATH` 宏指向同一份，与固件零漂移）。

字体子集由管线导出的字符集生成（**不要手工维护字符集**，漏字会静默显示成 □）：

```bash
npx lv_font_conv --font LXGWWenKai-Regular.ttf --size 24 --bpp 4 --format lvgl \
  --no-compress --lv-include lvgl.h --symbols="$(tr -d '\n' < main/assets/fonts/charset_hanzi_ui.txt)" \
  -o main/assets/fonts/lv_font_hanzi_ui_24.c
```

候选格的读音标注用的是独立的 20 px 小字体（a-z + 全部带调字符 + `.`）。24 px 放不下：最宽的 "chuáng" 要 83 px 墨迹，而拨盘候选格被环形触摸带卡死在 74 px 宽——20 px 时收缩到 70 px 才刚好单行放下：

```bash
npx lv_font_conv --font tools/hanzi_pipeline/.cache/fonts/LXGWWenKai-Regular.ttf --size 20 --bpp 4 --format lvgl \
  --no-compress --lv-include lvgl.h \
  --symbols='abcdefghijklmnopqrstuvwxyz.·àáèéêìíòóùúüāēěīńňōūǎǐǒǔǖǘǚǜǹḿếề' \
  -o main/assets/fonts/lv_font_pinyin_tone_20.c
```

## 主机验证

引擎不依赖设备，改动后应先在主机跑完这四个：

```bash
cmake -S tools/hanzi_host_test -B build_host && cmake --build build_host
./build_host/hanzi_host_test                   # 9562 字逐字与 golden 比对（missing 也算失败）
./build_host/hanzi_logic_test                  # 引擎不变量：round-trip + 差分参考实现 + 滚筒枚举双射
./build_host/anim_host_test                    # 默认字 + 笔画数/轮廓点极值字的残影检测
./build_host/hanzi_sim --out /tmp/hzsim        # 真 view 代码截图 + r=233 圆外像素断言
```

`anim_host_test` 每帧都会把增量绘制的结果与一次全量重绘做逐像素比对，必须完全一致——脏区算错、图层残留这类 bug 会在这里当场暴露。`hanzi_logic_test` 里最值钱的三条：9562 字的每个读音经"归一化→数字串→选中解释→查询"必须找回自己；引擎输出与测试内置的朴素参考实现逐项一致；滚筒枚举与音节表严格双射（每个音节可且仅可分解为首单元+后缀、每个枚举组合 queryExact 非空）——这条保证三列组合永远拨不出空候选列。`hanzi_sim` 的搜索段用脚本化触摸走真实 indev 管线：甩动惯性必须落在整行、轻点非选中行滚到位、换声母后缀保留/吸附、零声母 `·`、超长后缀 xiang、单候选音节不给惯性、点选中带的读音透传；三模式轮转段则断言：横滑手势经真实 GESTURE 派发切换模式且状态携带正确（滚筒 xiang → 拨盘前缀 xiang → T9 数字串 94264 选中解释 xiang → 滑回滚筒复原）、拨盘半截前缀 "xi" 进滚筒补全、空态切走落默认 "hao"、滚筒拨轮中/T9 按键中的横滑被抑制、模式脏标记一次性、setMode 恢复不置脏——每个模式的截图都过 r=233 圆外像素断言。另钉两个极端用例：数据里最宽的读音 "chuáng"（候选 caption 必须单行，历史上曾换行压到汉字上）和 "lv" 前缀（ü 通道全链路：环上亮 ü、echo 显示 lü、候选是 绿旅律率 而非 路）。

## 数据来源

- **笔顺数据**：[hanzi-writer-data](https://github.com/chanind/hanzi-writer) → 派生自 [Make Me a Hanzi](https://github.com/skishore/makemeahanzi)，其字形数据来自 **Arphic PL 字体**，依 **Arphic Public License** 授权，要求保留本声明。
- **教材字表与拼音**：[vipzhicheng/shukong-app](https://github.com/vipzhicheng/shukong-app) 整理的小学语文教材数据（MIT）。
- **一级字表与读音**：[jaywcjlove/table-of-general-standard-chinese-characters](https://github.com/jaywcjlove/table-of-general-standard-chinese-characters)（MIT）的《通用规范汉字表》数字化数据；读音常用度排序来自 [pypinyin](https://github.com/mozillazg/python-pinyin)（MIT），字频排名来自 [wordfreq](https://github.com/rspeer/wordfreq)（MIT）——三者只在重生成 `charlist.py` 时用到。
- **界面字体**：[霞鹜文楷 LXGW WenKai](https://github.com/lxgw/LxgwWenKai)，**SIL Open Font License 1.1**。选楷体是因为小学课本用楷体，且与笔顺数据的字形风格一致。

前两项在代码里有默认端点，直接跑就能拉；要换源就覆盖环境变量：

| 变量 | 指向 | 什么时候要 |
| --- | --- | --- |
| `$STROKE_URL` | 单字笔顺 JSON 的 URL 模板（一个 `{}` 占位符放字符） | `.cache/strokes/` 没缓存时 |
| `$BOOK_URL` | 分册字表 JSON 的 URL 模板（占位符放册号） | `.cache/books/` 没缓存时 |
| `$UI_FONT` | 生成子集字体用的 TTF | 字体不在 `.cache/fonts/` 时 |

笔顺 JSON 要有一个 `strokes` 数组，装 SVG 路径，坐标在 1024 单位的框里、基线 y=900；字表 JSON 每课要有 `recognition`（识字表）和 `writing`（写字表）两个字段，各自带逐字拼音。满足这两个形状的数据源都能直接接。

`.cache/` 暖起来之后就都不需要了。上述声明同时保留在生成物 `main/assets/hanzi/hanzi_data.h` 的文件头中。
