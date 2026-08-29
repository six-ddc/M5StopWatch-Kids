# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

给小朋友的学习机固件，跑在 M5Stack StopWatch（ESP32-S3，466×466 圆形触摸 AMOLED，16 MB flash + OPI PSRAM）上。硬件只有 A、B 两个键和一块触摸屏——所有交互设计都受这个约束支配。

## 构建与烧录

`components/` 被 gitignore，是脚本拉下来的，克隆后第一步：

```bash
python3 ./fetch_repos.py          # 按 repos.json 拉 M5GFX / LVGL v9.5 / mooncake 等，并打 patches/
idf.py build
idf.py -p <PORT> flash monitor
```

需要 ESP-IDF v5.5（`sdkconfig.defaults` 由 5.5.4 生成）。

**新增或删除源文件后必须先 `idf.py reconfigure`**——`main/CMakeLists.txt` 用 `file(GLOB_RECURSE)` 收源文件，不重新配置感知不到文件增删。**改 menuconfig 里的 App 开关同样要 reconfigure**。

### ⚠️ CMakeLists 已经不是一把梭 GLOB 了

`main/CMakeLists.txt` **按 App 分组**收集源文件，好让 menuconfig 里关掉一个 App 时它的代码和数据一起消失：

- 无条件进：`apps/common/*`、`hal/*`、`assets/fonts/lv_font_hanzi_ui_24.c`（launcher 和三个 App 都用它写字）
- `KIDS_APP_COUNT > 1` 才进：`apps/app_launcher/*` 和两个 indicator 图标
- 各 App 的 `apps/app_<x>/*` + 它独占的字体 / 图标，在各自的 `if(CONFIG_KIDS_APP_<X>)` 里
- `lv_font_hanzi_pinyin_44.c` 在 `if(HANZI OR ENGLISH)` 里——识字画拼音、英语画中文释义，两边都要
- blob 走 `EMBED_FILES`（`MY_EMBEDS`），不是编译成 C 数组

**新增 App、字体、图标或 blob，必须同时加进对应的条件分支。**没有兜底的 GLOB 捞剩下的文件——漏加的文件不会报错，只是静默不参与编译，然后在链接期变成一个莫名其妙的 undefined reference，或者更糟：运行时才发现某个资源没进固件。

`main/apps/build_config.h` 把 Kconfig 开关归一成 `KIDS_APP_COUNT` / `KIDS_STANDALONE`。要用 `KIDS_STANDALONE` 的 .cpp **必须能看到这个头**（三个 App 的 .h 都 include 了）——宏没定义时 `#if !KIDS_STANDALONE` 会取 `!0` 恒真，单 App 构建下那段代码照样编进去，而且不报任何错。

### 单 App 构建

只勾一个 App 时不装 launcher，`app_main` 直接 `installApp` + `openApp`。相应地三个 App 的 `GoHome` 分支里那句 `close()` 被 `#if !KIDS_STANDALONE` 包住了：没有 launcher 会重开它，关掉唯一的 App 只会剩一块黑屏。加新 App 时别忘了这一处。

一个 App 都不选会 `message(FATAL_ERROR)`。注意这个断言要带 `AND NOT CMAKE_BUILD_EARLY_EXPANSION`——组件 CMakeLists 会被求值两次，早期展开那次所有 `CONFIG_*` 都还没定义，不加保护会在正常配置时误报。

换 ESP-IDF 检出会让 CMake 缓存的源路径失效，只能 `rm -rf build` 全量重建（约 15~25 分钟，超过一般 shell 超时，后台跑再轮询日志）。

## 主机测试——改完先在主机跑，再上板

三个 App 的逻辑层都不含 LVGL / ESP-IDF 头，view 层则对着真 LVGL + 离屏 framebuffer 编译。这是本仓库最重要的工作流：**上板之前先跑完对应 App 的主机测试**，它们抓得到肉眼在真机上看不出来的问题。

```bash
# 识字
cmake -S tools/hanzi_host_test -B build_host && cmake --build build_host
./build_host/hanzi_host_test            # 9562 字逐字与 golden 逐像素比对（missing 也算失败）
./build_host/hanzi_logic_test           # T9 拼音引擎不变量：逐读音 round-trip + 朴素参考实现差分
./build_host/anim_host_test             # 增量绘制 vs 全量重绘逐帧比对；默认字 + 笔画/轮廓点极值字
./build_host/hanzi_sim --out /tmp/hzsim

# 算术
cmake -S tools/math_host_test -B build_math && cmake --build build_math
./build_math/math_logic_test            # 600 万道题的不变量断言 + 干扰项可归因性统计
./build_math/math_sim --out /tmp/mathshots

# 英语
cmake -S tools/english_host_test -B build_english && cmake --build build_english
./build_english/english_logic_test      # 直接读固件真正链接的那个 blob
./build_english/english_sim --out /tmp/engshots
```

`*_sim` 编译的是**真正的 view .cpp**，除了截图还会断言：**没有任何点亮的像素落在半径 233 的圆外**。屏幕是圆的，方形思维下排好的布局很容易在四角溢出玻璃，这个检查把它变成非零退出码。`english_sim` 还逐词检查每张图的点亮率落在 3%–97%（全黑=没画出来，全亮=palette 或 stride 错了），并自动挑出渲染最宽的词/释义去测极端布局。

## 架构

### App 生命周期

`main.cpp` 里 `installApp` 的顺序**就是** launcher 图标从左到右的顺序。App 都是 `mooncake::AppAbility` 子类，实现 `onCreate/onOpen/onRunning/onClose`。`AppLauncher` 继承 `AppLauncherBase`，交接由它负责——它在被选中的 App 打开前关掉自己、在那个 App 睡下后重开，所以**一个 App 想回首页只要 `close()` 自己**。（**单 App 构建里没有 launcher**，这条不成立，见上面「单 App 构建」。）

### 每个 App 三层，边界是硬的

```
game/ 或 engine/ 或 data/   纯逻辑，无 LVGL、无 ESP-IDF  → 主机可编译可测
view/*.cpp                  LVGL 页面，PSRAM 缓冲的所有权在这一层
app_*.cpp                   状态机 · 时钟 · 音频 · NVS · 反馈节奏
```

往 `game/` `engine/` `data/` 里塞任何 `lvgl.h` 或 `esp_*.h` 都会直接打断主机测试链路。反过来，一切"能被断言的算术"都应该住在这一层，而不是 `app_*.cpp`——比如星尘经济的全部计算在 `app_math/game/economy.*`，`AppMath` 只拥有时钟。

### HAL 与输入

`GetHAL()` 是全局单例（`main/hal/hal.h`），封装屏幕、触摸、按键、音频、震动、PMIC、RTC、NVS 设置。LVGL 不是线程安全的：**碰 LVGL 对象要拿锁**，用 `LvglLockGuard` 或 `lvglLock()/lvglUnlock()`。

`input::KeyManager` 把两个键归一成语义事件 `GoHome / GoPrevious / GoNext`（A+B 长按 = GoHome），App 不直接读按键状态。

进度用 `Settings`（`main/hal/utils/settings/settings.h`，NVS 包装）持久化，各 App 一个命名空间：`hanzi` / `math` / `english`。

## 圆屏与两个按键：布局的硬约束

这块屏是**圆的**（466 px，可视半径 233），两个按键像耳朵长在表壳的 **10 点和 2 点方向，距正上方约 ±41°**。方形思维排出来的布局在这里会出两类错，两类都出过：

**按键引导必须放在它对应的那个按键正下方**，不能把 A、B 两条并成一行摆在屏幕底部——底部那行谁也没指。算术 App 的三个页面都用同一组坐标：`kHintY = -145`、`kHintX = ±95`，A 在左、B 在右（见 `app_math/view/map.cpp`、`result.cpp`）。同理，**按键选中的东西也要放在按键正下方**：算术的答案卡在 `(±88, -95)`，英语的图卡在 `(±84, -40)`，这样"按左边的耳朵"和"选左边那张"在空间上是同一个动作，而不只是名义上的左右对应。

> 英语 App 三个页面一开始都把 `A x   B y` 并作一行放在底部，2026-08-21 才改过来。改的时候连带发现：顶部让给提示行（占 y=-160..-130）后，选组页的瓦片环要**整体下移 30 px**而不是缩半径——缩半径会把相邻瓦片的间隙从 17 px 挤到 7 px，八块瓦片糊成一团；下移则正好搬进底部空出来的位置。

物理布局拿不准时看 `M5StopWatch-UserDemo/main/assets/images/go_home_guide.c`，那张引导图画了按键位置，把 C 数组渲染出来就能量角度。

**圆屏上的宽度是量出来的，不是估出来的**：

- 别估文本宽度，用 `lv_text_get_size()`。按 glyph box 估会漏掉 advance 含的左右边距（低估约 20%），按 `0.55 × 字号` 估也一样偏小。
- 算某一行能用多宽，要用**墨迹实际的 y 范围**去反解弦长，不是 label 的包围盒，也不是中心那一行的 y。
- "最宽/最长/极端情况"的测试用例必须**从真实生成路径里采样**，不能手捏。手捏的 `88 + 99` 曾被用来测最宽算式，但加法上限是 99，生成器永远出不了这道题——拿不可能出现的输入测布局比不测更糟，给的是假的安全感。三个 `*_sim` 都改成采样几十万个真实样本再取最宽的那个。

三个 `*_sim` 都断言**没有任何点亮像素落在 r=233 外**，并把出界像素的包围盒打出来（截图是圆形遮罩后的，从 PPM 根本看不到出界的部分，只能靠断言报坐标）。改完布局跑一遍 sim，再把 `.ppm` 转成 PNG 看一眼——出界能被断言拦住，元素**互相重叠**拦不住，那个只能看。

## 几个必须遵守的固件约束

违反它们的症状都是"主机测试全绿但真机行为诡异"：

- **不能在持 LVGL 锁时写 NVS**，也不能在绘制回调里写：写 flash 会关 cache 并挂起另一个核。
- **`heap_caps_malloc` 不清零**——分配后必须 `memset`，否则未绘制像素在真机上是随机噪点。主机模拟器抓不到，因为静态内存被 loader 清零了。
- **打包的二进制数据必须逐字节读**：int16 坐标会落在奇数地址，Xtensa 对非对齐 32 位访问抛 `LoadStoreAlignmentError`。
- **每帧只发一个失效矩形**：LVGL 里每个失效区域都是一次完整渲染 pass。
- `LearnPage::update()`（识字）必须在持有 LVGL 锁时调用，它直接写活动的 canvas 缓冲。

## 资源与数据管线

资源不是手写的，是管线生成的。改数据格式要**同时**改管线的 `*format.py` 和固件侧解码器。

```bash
python3 tools/hanzi_pipeline/build_hanzi_data.py                 # 笔顺数据 + 字体字符集 + golden 参考图
python3 tools/hanzi_pipeline/make_char_table.py --source <csv>   # 仅重生成 charlist.py（全量字表+读音+字频）时用，
                                                                 # 依赖 pypinyin/wordfreq，日常构建不需要
python3 tools/english_pipeline/build_english_data.py             # ENG1 blob（图片 + 音频）
python3 tools/english_pipeline/build_english_data.py --pack-units N   # 只打进前 N 组，控制固件体积
python3 tools/*_pipeline/make_icon*.py                           # launcher 图标
```

- 中文字体子集由识字管线导出的字符集生成，**不要手工维护字符集**——漏字会静默显示成 □，没有任何报错。**写任何新的界面文案前先 `grep` 一下 `main/assets/fonts/charset_hanzi_ui.txt`**：字在里面就直接用；不在就得把文案加进 `tools/hanzi_pipeline/build_hanzi_data.py` 的 `UI_STRINGS`、重跑管线、再用 lv_font_conv 重新生成字体 .c（命令见 `app_hanzi/README.md`）。英语单词的中文释义不用管，它们由 `wordlist.py` 经 `english_glyphs()` 自动进字符集。
- 英语 blob 走 `EMBED_FILES` **链接**进固件，不是编译成 C 数组：480 词的数组形式是 43 MB 的 .c，GCC 每次都要嚼几分钟。`assets/english/english_data.h` 只是给链接器符号起名。
- 英语的图片音频全量缓存（约 26 MB）在 `tools/english_pipeline/.cache/` 且进版本库——换机器克隆下来即可构建，不需要任何外部输入，也不需要联网。管线的输入端点都走环境变量配置，代码里不写死。

## 分区与体积

不联网，没有 OTA 双分区。默认 `partitions.csv` 把 flash 全给 `factory`（15.9 MB）；`partitions-with-storage.csv` 是旧布局（`factory` 11 MB + `storage` 4.9 MB FAT），换布局只改 `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`，**代码不用动**——`hal_fs.cpp` 用 `esp_partition_find_first` 探测 storage 在不在，不在就跳过挂载。

那 4.9 MB 之前是白占的：`wear_levelling_init()` 把 FAT 挂上，但全仓库没有一行读写 `/spiflash`。要开始用文件系统时记得换回带 storage 的布局。

体积的大头是数据，不是代码：`hanzi_data.bin` 7.86 MB、`english_data.bin` 4.11 MB（24 组）/ 7.03 MB（40 组全量）。两条正交的裁剪路径——menuconfig 关掉整个 App，或者给英语管线加 `--pack-units N`。加内容前先看 `main/apps/app_english/README.md` 里的打包档位表。

**PSRAM 不是瓶颈**，别往那个方向优化：只有识字分配 PSRAM（canvas 300×300 RGB565 ≈ 176 KB + arena 16 KB + 候选缩略图与拼音索引 ~60 KB），算术和英语一个 `heap_caps_malloc` 都没有。加上 LVGL cache 1 MB 和显示缓冲，三个 App 全开也用不到 8 MB 里的 1.5 MB。

## 风格

CI 用 clang-format 22 检查（`.github/workflows/clang-format-check.yml`，配置见 `.clang-format`，排除 `assets/` 和 `hal/drivers/`）。源码注释是英文，设计文档（README）是中文。

## 更详细的设计说明

每个 App 的 README 记的是**为什么**这么做，改动前值得读：

- `main/apps/app_hanzi/README.md` — 光栅化与合成规则、动画残影检测
- `main/apps/app_math/README.md` — 干扰项为什么必须是真实错解、启用条件比错解类型更要紧、自适应与地图
- `main/apps/app_english/README.md` — 为什么全是具象词、I4 图片格式、音频抽取的坑、144 px 怎么算出来的
