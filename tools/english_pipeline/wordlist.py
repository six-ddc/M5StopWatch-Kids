#!/usr/bin/env python3
"""The word list the English app teaches: 40 units, 12 words each.

Every word here is concrete -- something a five-year-old can point at. That is
not a style preference, it is a hard constraint: the app teaches a word by
showing one picture, and function words (the, was, could) have no picture. If a
word cannot be drawn it does not belong in this file.

Coverage
--------
The spine is a primary-school vocabulary for grades 3-6, filled out with
the kindergarten words that come earlier (family, food, animals, toys). Where a
theme is mostly abstract it is simply not here -- there is no unit for
weekdays, months, or ordinals, because "Wednesday" and "third" have no picture
either.

Selection rules
---------------
  * kindergarten .. grade 6 vocabulary
  * the picture library has a clear, unambiguous pictogram for it
  * the dictionary has a headword recording for it (see build_english_data.py for how
    the recording is located -- the mdd does not carry an en_us_ file for every
    word, so some fall back to the British headword clip)
  * the Chinese gloss is 1-4 characters, the way a textbook would gloss it
  * no spaces in the headword: the audio lookup keys off it, and the mdd has no
    en_us_ice cream.mp3

The picture is the veto
-----------------------
Four themes were drafted, built, looked at, and thrown away, which is worth
recording so nobody drafts them again:

  * prepositions (in / on / under / behind / near / far). The library draws these
    as an orange square with a second orange square somewhere near it. `on` and
    `under` are the same tile. `far` is a red dot 0.4% of the frame.
  * the smaller body parts (neck / shoulder / chest / knee / elbow / chin). All
    of them are a skin-coloured patch on black with no body around it; you
    cannot tell an elbow from a cheek. The twelve in the 身体 unit above are
    the ones that have a silhouette of their own.
  * day parts and seasons (morning / afternoon / evening / day). Sun-on-the-
    horizon icons a few percent lit, plus a calendar page for `day`; the season
    pictograms are an icon next to three red squares and `winter` is just the
    squares.
  * big / small / long / short / fast / slow, for the reason noted on the
    形容词 unit.

`pic` pins the pictogram id
-------------------------
Search ranking drifts, and for a fair number of words the top hit is wrong in a
way only a human notices: `plane` returns a carpenter's plane, `hand` returns
the *verb* (a hand offering something), `water` returns someone watering a
plant, `grape` returns a grapevine. The expansion turned up a whole second
crop of these -- `triangle` returns the percussion instrument, `mouse` the
computer peripheral, `swing` a ram, `boxing` a cardboard box, `key` a hand
typing, `octopus` a scuba regulator, `rock` a woman rocking a baby, `eraser` a
rubber boot. Every id below was eyeballed once at the final 144x144 quantised
size, so the build pins it instead of re-searching. Leave `pic` unset and the
builder falls back to search -- useful when adding a word, but pin it before
committing.

`bg` overrides the black matte
------------------------------
Images are composited onto black because the panel is black AMOLED: the
pictogram then looks like it floats on the screen. That fails for art drawn as
a black line on a transparent ground, which comes out as a blank tile. `black`
was the first case -- the library draws it as a pure #000000 paint splat. The
数字, 形状 and 昆虫 units are the rest of them, and they take the white card
as a whole unit so the quiz never puts a white tile next to a black one.

`sfx` marks words that *deserve* a sound effect (a moo, a siren) for the
listen-and-pick game. There is no sound-effect source wired up yet, so nothing
reads it at build time except the manifest; it is here so the word list does
not have to be revisited when one is.
"""

# Unit = (Chinese title, [word, ...]); Word = dict with:
#   en   lower-case English headword (also the audio lookup key)
#   zh   Chinese gloss shown under the picture
#   pic  pinned pictogram id (omit to let the builder search)
#   sfx  True if a sound effect would suit this word (future work)
#   bg   "#RRGGBB" matte override (default black)

UNITS = [
    ("动物", [
        {"en": "cat",       "zh": "猫",     "pic": 7114,  "sfx": True},
        {"en": "dog",       "zh": "狗",     "pic": 7202,  "sfx": True},
        {"en": "bird",      "zh": "鸟",     "pic": 2490,  "sfx": True},
        {"en": "fish",      "zh": "鱼",     "pic": 2520},
        {"en": "cow",       "zh": "奶牛",   "pic": 2609,  "sfx": True},
        {"en": "pig",       "zh": "猪",     "pic": 24972, "sfx": True},
        {"en": "duck",      "zh": "鸭子",   "pic": 28479, "sfx": True},
        {"en": "sheep",     "zh": "绵羊",   "pic": 2489,  "sfx": True},
        {"en": "horse",     "zh": "马",     "pic": 2294,  "sfx": True},
        {"en": "lion",      "zh": "狮子",   "pic": 25187, "sfx": True},
        {"en": "elephant",  "zh": "大象",   "pic": 2372,  "sfx": True},
        {"en": "monkey",    "zh": "猴子",   "pic": 2477,  "sfx": True},
    ]),
    ("食物", [
        {"en": "apple",     "zh": "苹果",   "pic": 2462},
        {"en": "banana",    "zh": "香蕉",   "pic": 2530},
        {"en": "orange",    "zh": "橙子",   "pic": 2483},
        {"en": "grape",     "zh": "葡萄",   "pic": 3247},
        {"en": "bread",     "zh": "面包",   "pic": 2494},
        {"en": "cake",      "zh": "蛋糕",   "pic": 2579},
        {"en": "egg",       "zh": "鸡蛋",   "pic": 2427},
        {"en": "milk",      "zh": "牛奶",   "pic": 2445},
        {"en": "rice",      "zh": "米饭",   "pic": 39387},
        {"en": "water",     "zh": "水",     "pic": 2248},
        {"en": "cheese",    "zh": "奶酪",   "pic": 2541},
        {"en": "cookie",    "zh": "饼干",   "pic": 8312},
    ]),
    # The colour pictograms are all the same paint splat in different inks,
    # which is exactly what you want here -- nothing in the picture competes
    # with the colour itself. `rainbow` closes the unit because it is the one
    # colour word that is also a thing.
    ("颜色", [
        {"en": "red",       "zh": "红色",   "pic": 2808},
        {"en": "orange",    "zh": "橙色",   "pic": 2888},
        {"en": "yellow",    "zh": "黄色",   "pic": 2648},
        {"en": "green",     "zh": "绿色",   "pic": 4887},
        {"en": "blue",      "zh": "蓝色",   "pic": 4869},
        {"en": "purple",    "zh": "紫色",   "pic": 2907},
        {"en": "pink",      "zh": "粉色",   "pic": 2807},
        {"en": "brown",     "zh": "棕色",   "pic": 2923},
        {"en": "black",     "zh": "黑色",   "pic": 2886, "bg": "#FFFFFF"},
        {"en": "white",     "zh": "白色",   "pic": 8043},
        {"en": "gray",      "zh": "灰色",   "pic": 3340},
        {"en": "rainbow",   "zh": "彩虹",   "pic": 2986},
    ]),
    # `ship` was the obvious twelfth word and is deliberately absent: every
    # The ship is drawn sitting in a rectangle of blue water, which on a
    # black panel reads as a blue box rather than a cut-out. `rocket` is a
    # clean cut-out, and a five-year-old would rather have it anyway.
    ("交通", [
        {"en": "car",        "zh": "小汽车", "pic": 2339,  "sfx": True},
        {"en": "bus",        "zh": "公交车", "pic": 2262},
        {"en": "bike",       "zh": "自行车", "pic": 2277},
        {"en": "motorcycle", "zh": "摩托车", "pic": 7166,  "sfx": True},
        {"en": "train",      "zh": "火车",   "pic": 2603,  "sfx": True},
        {"en": "airplane",   "zh": "飞机",   "pic": 6924,  "sfx": True},
        {"en": "helicopter", "zh": "直升机", "pic": 7126,  "sfx": True},
        {"en": "boat",       "zh": "小船",   "pic": 6932},
        {"en": "rocket",     "zh": "火箭",   "pic": 2344,  "sfx": True},
        {"en": "truck",      "zh": "卡车",   "pic": 3232},
        {"en": "taxi",       "zh": "出租车", "pic": 2580},
        {"en": "ambulance",  "zh": "救护车", "pic": 6899,  "sfx": True},
    ]),
    ("身体", [
        {"en": "head",      "zh": "头",     "pic": 2673},
        {"en": "face",      "zh": "脸",     "pic": 2684},
        {"en": "hair",      "zh": "头发",   "pic": 2851},
        {"en": "eye",       "zh": "眼睛",   "pic": 6573},
        {"en": "ear",       "zh": "耳朵",   "pic": 2871},
        {"en": "nose",      "zh": "鼻子",   "pic": 2887},
        {"en": "mouth",     "zh": "嘴巴",   "pic": 2663},
        {"en": "tooth",     "zh": "牙齿",   "pic": 10267},
        {"en": "hand",      "zh": "手",     "pic": 2928},
        {"en": "arm",       "zh": "手臂",   "pic": 2669},
        {"en": "leg",       "zh": "腿",     "pic": 8666},
        {"en": "foot",      "zh": "脚",     "pic": 25327},
    ]),
    ("学习用品", [
        {"en": "pencil",   "zh": "铅笔",   "pic": 2440},
        {"en": "pen",      "zh": "钢笔",   "pic": 10313},
        {"en": "eraser",   "zh": "橡皮",   "pic": 2409},
        {"en": "ruler",    "zh": "尺子",   "pic": 2815},
        {"en": "book",     "zh": "书",     "pic": 25191},
        {"en": "notebook", "zh": "本子",   "pic": 7142},
        {"en": "paper",    "zh": "纸",     "pic": 8349},
        {"en": "backpack", "zh": "书包",   "pic": 2475},
        {"en": "scissors", "zh": "剪刀",   "pic": 2591},
        {"en": "glue",     "zh": "胶水",   "pic": 2709},
        {"en": "folder",   "zh": "文件夹", "pic": 3233},
        {"en": "stapler",  "zh": "订书机", "pic": 2413},
    ]),
    ("教室", [
        {"en": "desk",       "zh": "课桌",   "pic": 36285},
        {"en": "chair",      "zh": "椅子",   "pic": 3155},
        {"en": "blackboard", "zh": "黑板",   "pic": 2526},
        {"en": "chalk",      "zh": "粉笔",   "pic": 4965},
        {"en": "door",       "zh": "门",     "pic": 3244},
        {"en": "window",     "zh": "窗",     "pic": 2611},
        {"en": "clock",      "zh": "钟",     "pic": 5561},
        {"en": "map",        "zh": "地图",   "pic": 5505},
        {"en": "globe",      "zh": "地球仪", "pic": 27798},
        {"en": "flag",       "zh": "旗子",   "pic": 5918},
        {"en": "computer",   "zh": "电脑",   "pic": 7190},
        {"en": "bell",       "zh": "铃铛",   "pic": 5949,  "sfx": True},
    ]),
    # uncle and aunt were the obvious next two and are both absent: the library
    # draws them as the same uncoloured outline family group, so they are
    # indistinguishable from each other on a 144 px tile.
    ("家人", [
        {"en": "father",      "zh": "爸爸", "pic": 2497},
        {"en": "mother",      "zh": "妈妈", "pic": 2458},
        {"en": "brother",     "zh": "哥哥", "pic": 2423},
        {"en": "sister",      "zh": "姐姐", "pic": 2422},
        {"en": "baby",        "zh": "宝宝", "pic": 6060,  "sfx": True},
        {"en": "grandfather", "zh": "爷爷", "pic": 23718},
        {"en": "grandmother", "zh": "奶奶", "pic": 23710},
        {"en": "man",         "zh": "男人", "pic": 4665},
        {"en": "woman",       "zh": "女人", "pic": 24621},
        {"en": "family",      "zh": "家人", "pic": 2392},
        {"en": "boy",         "zh": "男孩", "pic": 7176},
        {"en": "girl",        "zh": "女孩", "pic": 27509},
    ]),
    ("房子", [
        {"en": "house",    "zh": "房子", "pic": 2317},
        {"en": "bedroom",  "zh": "卧室", "pic": 5988},
        {"en": "kitchen",  "zh": "厨房", "pic": 10753},
        {"en": "bathroom", "zh": "浴室", "pic": 15905},
        {"en": "toilet",   "zh": "马桶", "pic": 2430},
        {"en": "garden",   "zh": "花园", "pic": 2974},
        {"en": "garage",   "zh": "车库", "pic": 16975},
        {"en": "roof",     "zh": "屋顶", "pic": 2584},
        {"en": "chimney",  "zh": "烟囱", "pic": 2333},
        {"en": "gate",     "zh": "大门", "pic": 37619},
        {"en": "fence",    "zh": "栅栏", "pic": 6651},
        {"en": "stairs",   "zh": "楼梯", "pic": 2379},
    ]),
    ("家具", [
        {"en": "bed",      "zh": "床",   "pic": 2304},
        {"en": "table",    "zh": "桌子", "pic": 3129},
        {"en": "sofa",     "zh": "沙发", "pic": 2571},
        {"en": "lamp",     "zh": "台灯", "pic": 4936},
        {"en": "mirror",   "zh": "镜子", "pic": 8573},
        {"en": "curtain",  "zh": "窗帘", "pic": 8344},
        {"en": "carpet",   "zh": "地毯", "pic": 2249},
        {"en": "drawer",   "zh": "抽屉", "pic": 6070},
        {"en": "cupboard", "zh": "橱柜", "pic": 23753},
        {"en": "wardrobe", "zh": "衣柜", "pic": 2258},
        {"en": "pillow",   "zh": "枕头", "pic": 2250},
        {"en": "blanket",  "zh": "毯子", "pic": 2459},
    ]),
    ("衣服", [
        {"en": "shirt",   "zh": "衬衫",   "pic": 13640},
        {"en": "dress",   "zh": "连衣裙", "pic": 2613},
        {"en": "skirt",   "zh": "裙子",   "pic": 2391},
        {"en": "pants",   "zh": "裤子",   "pic": 2565},
        {"en": "pajamas", "zh": "睡衣",   "pic": 2522},
        {"en": "coat",    "zh": "外套",   "pic": 2242},
        {"en": "jacket",  "zh": "夹克",   "pic": 4872},
        {"en": "sweater", "zh": "毛衣",   "pic": 2436},
        {"en": "socks",   "zh": "袜子",   "pic": 8339},
        {"en": "shoes",   "zh": "鞋子",   "pic": 32922},
        {"en": "hat",     "zh": "帽子",   "pic": 2572},
        {"en": "cap",     "zh": "鸭舌帽", "pic": 39395},
    ]),
    ("配饰", [
        {"en": "gloves",   "zh": "手套", "pic": 8353},
        {"en": "scarf",    "zh": "围巾", "pic": 2290},
        {"en": "boots",    "zh": "靴子", "pic": 8299},
        {"en": "belt",     "zh": "腰带", "pic": 2336},
        {"en": "tie",      "zh": "领带", "pic": 4614},
        {"en": "glasses",  "zh": "眼镜", "pic": 30186},
        {"en": "umbrella", "zh": "雨伞", "pic": 2500},
        {"en": "watch",    "zh": "手表", "pic": 2549},
        {"en": "helmet",   "zh": "头盔", "pic": 2691},
        {"en": "ring",     "zh": "戒指", "pic": 2252},
        {"en": "button",   "zh": "纽扣", "pic": 2668},
        {"en": "comb",     "zh": "梳子", "pic": 2852},
    ]),
    ("水果", [
        {"en": "pear",       "zh": "梨",     "pic": 2561},
        {"en": "peach",      "zh": "桃子",   "pic": 2468},
        {"en": "watermelon", "zh": "西瓜",   "pic": 2557},
        {"en": "strawberry", "zh": "草莓",   "pic": 2400},
        {"en": "lemon",      "zh": "柠檬",   "pic": 3022},
        {"en": "mango",      "zh": "芒果",   "pic": 16813},
        {"en": "pineapple",  "zh": "菠萝",   "pic": 2525},
        {"en": "cherry",     "zh": "樱桃",   "pic": 8303},
        {"en": "coconut",    "zh": "椰子",   "pic": 10224},
        {"en": "plum",       "zh": "李子",   "pic": 8305},
        {"en": "melon",      "zh": "甜瓜",   "pic": 36153},
        {"en": "kiwi",       "zh": "猕猴桃", "pic": 2955},
    ]),
    ("蔬菜", [
        {"en": "tomato",   "zh": "西红柿", "pic": 2594},
        {"en": "potato",   "zh": "土豆",   "pic": 2503},
        {"en": "carrot",   "zh": "胡萝卜", "pic": 2619},
        {"en": "onion",    "zh": "洋葱",   "pic": 2323},
        {"en": "cabbage",  "zh": "白菜",   "pic": 2708},
        {"en": "cucumber", "zh": "黄瓜",   "pic": 2847},
        {"en": "pumpkin",  "zh": "南瓜",   "pic": 2679},
        {"en": "corn",     "zh": "玉米",   "pic": 36287},
        {"en": "mushroom", "zh": "蘑菇",   "pic": 10220},
        {"en": "pepper",   "zh": "辣椒",   "pic": 2838},
        {"en": "garlic",   "zh": "大蒜",   "pic": 2641},
        {"en": "lettuce",  "zh": "生菜",   "pic": 2446},
    ]),
    ("饭菜", [
        {"en": "noodles",   "zh": "面条",   "pic": 8584},
        {"en": "soup",      "zh": "汤",     "pic": 2573},
        {"en": "salad",     "zh": "沙拉",   "pic": 2377},
        {"en": "sandwich",  "zh": "三明治", "pic": 2281},
        {"en": "hamburger", "zh": "汉堡",   "pic": 2419},
        {"en": "pizza",     "zh": "披萨",   "pic": 2527},
        {"en": "chicken",   "zh": "鸡肉",   "pic": 4952},
        {"en": "beef",      "zh": "牛肉",   "pic": 25630},
        {"en": "ham",       "zh": "火腿",   "pic": 2433},
        {"en": "sausage",   "zh": "香肠",   "pic": 6647},
        {"en": "pasta",     "zh": "意面",   "pic": 8652},
        {"en": "toast",     "zh": "吐司",   "pic": 17330},
    ]),
    ("零食饮料", [
        {"en": "juice",     "zh": "果汁",   "pic": 11461},
        {"en": "tea",       "zh": "茶",     "pic": 29802},
        {"en": "coffee",    "zh": "咖啡",   "pic": 24479},
        {"en": "yogurt",    "zh": "酸奶",   "pic": 2618},
        {"en": "jam",       "zh": "果酱",   "pic": 2470},
        {"en": "butter",    "zh": "黄油",   "pic": 2461},
        {"en": "honey",     "zh": "蜂蜜",   "pic": 2911},
        {"en": "sugar",     "zh": "糖",     "pic": 25560},
        {"en": "popcorn",   "zh": "爆米花", "pic": 5534},
        {"en": "donut",     "zh": "甜甜圈", "pic": 2368},
        {"en": "chocolate", "zh": "巧克力", "pic": 2334},
        {"en": "lollipop",  "zh": "棒棒糖", "pic": 2832},
    ]),
    ("餐具", [
        {"en": "cup",        "zh": "杯子",   "pic": 2582},
        {"en": "glass",      "zh": "玻璃杯", "pic": 2610},
        {"en": "plate",      "zh": "盘子",   "pic": 16857},
        {"en": "bowl",       "zh": "碗",     "pic": 3257},
        {"en": "fork",       "zh": "叉子",   "pic": 2588},
        {"en": "knife",      "zh": "刀",     "pic": 4931},
        {"en": "spoon",      "zh": "勺子",   "pic": 2362},
        {"en": "chopsticks", "zh": "筷子",   "pic": 36564},
        {"en": "bottle",     "zh": "瓶子",   "pic": 2288},
        {"en": "napkin",     "zh": "餐巾",   "pic": 36303},
        {"en": "pot",        "zh": "锅",     "pic": 9086},
        {"en": "basket",     "zh": "篮子",   "pic": 3012},
    ]),
    # sunny / rainy / cloudy are missing on purpose: all three are the same
    # drawing of the same house with a different sky, which makes a two-way
    # quiz a coin toss. `fog` went too -- its pictogram is a uniform grey
    # rectangle. The unit takes the night sky instead, which is more picture
    # per word.
    ("天气", [
        {"en": "sun",       "zh": "太阳", "pic": 2798},
        {"en": "rain",      "zh": "雨",   "pic": 3123,  "sfx": True},
        {"en": "snow",      "zh": "雪",   "pic": 3135},
        {"en": "wind",      "zh": "风",   "pic": 23731, "sfx": True},
        {"en": "cloud",     "zh": "云",   "pic": 2883},
        {"en": "ice",       "zh": "冰",   "pic": 7128},
        {"en": "lightning", "zh": "闪电", "pic": 34545, "sfx": True},
        {"en": "snowman",   "zh": "雪人", "pic": 3131},
        {"en": "moon",      "zh": "月亮", "pic": 2933},
        {"en": "star",      "zh": "星星", "pic": 2752},
        {"en": "sky",       "zh": "天空", "pic": 38270},
        {"en": "night",     "zh": "夜晚", "pic": 35789},
    ]),
    ("植物", [
        {"en": "tree",      "zh": "树",     "pic": 3057},
        {"en": "flower",    "zh": "花",     "pic": 3102},
        {"en": "grass",     "zh": "草",     "pic": 3113},
        {"en": "leaf",      "zh": "叶子",   "pic": 5077},
        {"en": "seed",      "zh": "种子",   "pic": 8689},
        {"en": "branch",    "zh": "树枝",   "pic": 7224},
        {"en": "trunk",     "zh": "树干",   "pic": 7819},
        {"en": "forest",    "zh": "森林",   "pic": 2666},
        {"en": "rose",      "zh": "玫瑰",   "pic": 3151},
        {"en": "sunflower", "zh": "向日葵", "pic": 11274},
        {"en": "daisy",     "zh": "雏菊",   "pic": 3127},
        {"en": "cactus",    "zh": "仙人掌", "pic": 3070},
    ]),
    ("山水", [
        {"en": "mountain",  "zh": "山",   "pic": 2909},
        {"en": "river",     "zh": "河",   "pic": 2811},
        {"en": "lake",      "zh": "湖",   "pic": 6022},
        {"en": "sea",       "zh": "大海", "pic": 2925,  "sfx": True},
        {"en": "beach",     "zh": "沙滩", "pic": 2826},
        {"en": "sand",      "zh": "沙子", "pic": 4565},
        {"en": "rock",      "zh": "石头", "pic": 6594},
        {"en": "island",    "zh": "小岛", "pic": 2966},
        {"en": "waterfall", "zh": "瀑布", "pic": 5951,  "sfx": True},
        {"en": "hill",      "zh": "小山", "pic": 38728},
        {"en": "desert",    "zh": "沙漠", "pic": 2734},
        {"en": "cave",      "zh": "山洞", "pic": 2729},
    ]),
    # The numeral pictograms are black glyphs on a transparent ground, so the
    # whole unit takes a white card -- on the default black matte every tile
    # here is a blank rectangle. `ten` is the one that also shows two hands,
    # which is a happier picture than a bare "10" anyway.
    ("数字", [
        {"en": "one",    "zh": "一",   "pic": 2627, "bg": "#FFFFFF"},
        {"en": "two",    "zh": "二",   "pic": 2628, "bg": "#FFFFFF"},
        {"en": "three",  "zh": "三",   "pic": 2629, "bg": "#FFFFFF"},
        {"en": "four",   "zh": "四",   "pic": 2630, "bg": "#FFFFFF"},
        {"en": "five",   "zh": "五",   "pic": 2631, "bg": "#FFFFFF"},
        {"en": "six",    "zh": "六",   "pic": 2632, "bg": "#FFFFFF"},
        {"en": "seven",  "zh": "七",   "pic": 2633, "bg": "#FFFFFF"},
        {"en": "eight",  "zh": "八",   "pic": 2634, "bg": "#FFFFFF"},
        {"en": "nine",   "zh": "九",   "pic": 2635, "bg": "#FFFFFF"},
        {"en": "ten",    "zh": "十",   "pic": 7025, "bg": "#FFFFFF"},
        {"en": "eleven", "zh": "十一", "pic": 29260, "bg": "#FFFFFF"},
        {"en": "twelve", "zh": "十二", "pic": 29262, "bg": "#FFFFFF"},
    ]),
    # `cook` is the person in the chef hat, not the pictogram of the verb --
    # the top search hit for "cook" is a hand stirring a pot.
    ("职业", [
        {"en": "teacher",     "zh": "老师",   "pic": 2457},
        {"en": "doctor",      "zh": "医生",   "pic": 2467},
        {"en": "nurse",       "zh": "护士",   "pic": 2375},
        {"en": "farmer",      "zh": "农民",   "pic": 2982},
        {"en": "cook",        "zh": "厨师",   "pic": 6985},
        {"en": "driver",      "zh": "司机",   "pic": 3019},
        {"en": "pilot",       "zh": "飞行员", "pic": 3370},
        {"en": "police",      "zh": "警察",   "pic": 11345, "sfx": True},
        {"en": "firefighter", "zh": "消防员", "pic": 2664,  "sfx": True},
        {"en": "singer",      "zh": "歌手",   "pic": 4585},
        {"en": "dancer",      "zh": "舞蹈家", "pic": 11186},
        {"en": "waiter",      "zh": "服务员", "pic": 11198},
    ]),
    ("运动", [
        {"en": "football",   "zh": "足球",   "pic": 35256},
        {"en": "basketball", "zh": "篮球",   "pic": 10166},
        {"en": "volleyball", "zh": "排球",   "pic": 10167},
        {"en": "tennis",     "zh": "网球",   "pic": 10158},
        {"en": "baseball",   "zh": "棒球",   "pic": 8660},
        {"en": "badminton",  "zh": "羽毛球", "pic": 4969},
        {"en": "golf",       "zh": "高尔夫", "pic": 10160},
        {"en": "bowling",    "zh": "保龄球", "pic": 30020},
        {"en": "hockey",     "zh": "冰球",   "pic": 10161},
        {"en": "boxing",     "zh": "拳击",   "pic": 8981},
        {"en": "skating",    "zh": "滑冰",   "pic": 8318},
        {"en": "rugby",      "zh": "橄榄球", "pic": 21949},
    ]),
    ("玩具", [
        {"en": "ball",       "zh": "球",     "pic": 3241},
        {"en": "balloon",    "zh": "气球",   "pic": 2408},
        {"en": "kite",       "zh": "风筝",   "pic": 2350},
        {"en": "doll",       "zh": "娃娃",   "pic": 26238},
        {"en": "robot",      "zh": "机器人", "pic": 6208,  "sfx": True},
        {"en": "puzzle",     "zh": "拼图",   "pic": 2540},
        {"en": "teddy",      "zh": "泰迪熊", "pic": 4945},
        {"en": "marble",     "zh": "弹珠",   "pic": 36507},
        {"en": "swing",      "zh": "秋千",   "pic": 4608},
        {"en": "slide",      "zh": "滑梯",   "pic": 4759},
        {"en": "skateboard", "zh": "滑板",   "pic": 2507},
        {"en": "scooter",    "zh": "踏板车", "pic": 2508},
    ]),
    ("乐器", [
        {"en": "piano",      "zh": "钢琴",   "pic": 2521,  "sfx": True},
        {"en": "guitar",     "zh": "吉他",   "pic": 2417,  "sfx": True},
        {"en": "drum",       "zh": "鼓",     "pic": 2578,  "sfx": True},
        {"en": "violin",     "zh": "小提琴", "pic": 2615,  "sfx": True},
        {"en": "flute",      "zh": "长笛",   "pic": 2396,  "sfx": True},
        {"en": "trumpet",    "zh": "小号",   "pic": 2607,  "sfx": True},
        {"en": "harp",       "zh": "竖琴",   "pic": 8493,  "sfx": True},
        {"en": "saxophone",  "zh": "萨克斯", "pic": 2559,  "sfx": True},
        {"en": "accordion",  "zh": "手风琴", "pic": 5895,  "sfx": True},
        {"en": "tambourine", "zh": "铃鼓",   "pic": 2564,  "sfx": True},
        {"en": "xylophone",  "zh": "木琴",   "pic": 2616,  "sfx": True},
        {"en": "harmonica",  "zh": "口琴",   "pic": 5909,  "sfx": True},
    ]),
    ("日常动作", [
        {"en": "eat",   "zh": "吃",   "pic": 2349},
        {"en": "drink", "zh": "喝",   "pic": 4575},
        {"en": "sleep", "zh": "睡觉", "pic": 2369},
        {"en": "wash",  "zh": "洗",   "pic": 3323},
        {"en": "clean", "zh": "打扫", "pic": 17269},
        {"en": "read",  "zh": "读",   "pic": 28643},
        {"en": "write", "zh": "写",   "pic": 2380},
        {"en": "draw",  "zh": "画画", "pic": 8088},
        {"en": "sing",  "zh": "唱歌", "pic": 2315},
        {"en": "dance", "zh": "跳舞", "pic": 2652},
        {"en": "play",  "zh": "玩",   "pic": 2439},
        {"en": "cut",   "zh": "剪",   "pic": 5975},
    ]),
    ("身体动作", [
        {"en": "run",   "zh": "跑",   "pic": 2719},
        {"en": "walk",  "zh": "走",   "pic": 3251},
        {"en": "jump",  "zh": "跳",   "pic": 2804},
        {"en": "fly",   "zh": "飞",   "pic": 2478},
        {"en": "swim",  "zh": "游泳", "pic": 24903},
        {"en": "climb", "zh": "爬",   "pic": 8226},
        {"en": "ride",  "zh": "骑",   "pic": 6643},
        {"en": "sit",   "zh": "坐",   "pic": 25082},
        {"en": "stand", "zh": "站",   "pic": 13368},
        {"en": "push",  "zh": "推",   "pic": 4638},
        {"en": "pull",  "zh": "拉",   "pic": 5569},
        {"en": "throw", "zh": "扔",   "pic": 6542},
    ]),
    # Same white card, same reason: the library draws the flat shapes as an
    # unfilled black outline. `cone` is deliberately absent -- its pictogram
    # is an ice-cream cone. The three solids at the end are already coloured
    # in, and read fine either way, so the unit stays visually consistent.
    ("形状", [
        {"en": "circle",    "zh": "圆形",   "pic": 4603, "bg": "#FFFFFF"},
        {"en": "square",    "zh": "正方形", "pic": 4616, "bg": "#FFFFFF"},
        {"en": "triangle",  "zh": "三角形", "pic": 4763, "bg": "#FFFFFF"},
        {"en": "rectangle", "zh": "长方形", "pic": 4731, "bg": "#FFFFFF"},
        {"en": "oval",      "zh": "椭圆形", "pic": 4711, "bg": "#FFFFFF"},
        {"en": "heart",     "zh": "心形",   "pic": 4613, "bg": "#FFFFFF"},
        {"en": "diamond",   "zh": "菱形",   "pic": 6475, "bg": "#FFFFFF"},
        {"en": "pentagon",  "zh": "五边形", "pic": 4715, "bg": "#FFFFFF"},
        {"en": "hexagon",   "zh": "六边形", "pic": 4663, "bg": "#FFFFFF"},
        {"en": "cube",      "zh": "立方体", "pic": 8308, "bg": "#FFFFFF"},
        {"en": "cylinder",  "zh": "圆柱",   "pic": 9111, "bg": "#FFFFFF"},
        {"en": "sphere",    "zh": "球体",   "pic": 9113, "bg": "#FFFFFF"},
    ]),
    # Every adjective here is a picture of a *thing* in two states -- a clean
    # sock and a filthy one, a full bottle and an empty one. big/small,
    # long/short and fast/slow were cut: the library draws those as a pair of
    # bare rectangles that only mean anything side by side, and the quiz
    # shows one picture at a time.
    ("形容词", [
        {"en": "tall",  "zh": "高", "pic": 4557},
        {"en": "fat",   "zh": "胖", "pic": 4656},
        {"en": "thin",  "zh": "瘦", "pic": 10200},
        {"en": "new",   "zh": "新", "pic": 4705},
        {"en": "old",   "zh": "旧", "pic": 4770},
        {"en": "hot",   "zh": "热", "pic": 2300},
        {"en": "cold",  "zh": "冷", "pic": 4652},
        {"en": "wet",   "zh": "湿", "pic": 4697},
        {"en": "dry",   "zh": "干", "pic": 2566},
        {"en": "dirty", "zh": "脏", "pic": 4750},
        {"en": "full",  "zh": "满", "pic": 4688},
        {"en": "empty", "zh": "空", "pic": 4767},
    ]),
    ("场所", [
        {"en": "school",     "zh": "学校",   "pic": 3082},
        {"en": "hospital",   "zh": "医院",   "pic": 3116},
        {"en": "park",       "zh": "公园",   "pic": 5379},
        {"en": "shop",       "zh": "商店",   "pic": 35695},
        {"en": "market",     "zh": "市场",   "pic": 32942},
        {"en": "library",    "zh": "图书馆", "pic": 3065},
        {"en": "museum",     "zh": "博物馆", "pic": 3132},
        {"en": "zoo",        "zh": "动物园", "pic": 4773},
        {"en": "farm",       "zh": "农场",   "pic": 3337},
        {"en": "bank",       "zh": "银行",   "pic": 3062},
        {"en": "restaurant", "zh": "餐馆",   "pic": 10283},
        {"en": "airport",    "zh": "机场",   "pic": 3053},
    ]),
    # `key` is the key ring: the plain key pictogram is a black outline that
    # vanishes on the matte, and the other "key" hit is a hand typing.
    ("日用品", [
        {"en": "soap",        "zh": "肥皂",   "pic": 8094},
        {"en": "towel",       "zh": "毛巾",   "pic": 2593},
        {"en": "toothbrush",  "zh": "牙刷",   "pic": 2694},
        {"en": "toothpaste",  "zh": "牙膏",   "pic": 2858},
        {"en": "candle",      "zh": "蜡烛",   "pic": 6242},
        {"en": "broom",       "zh": "扫把",   "pic": 2693},
        {"en": "rope",        "zh": "绳子",   "pic": 7006},
        {"en": "key",         "zh": "钥匙",   "pic": 28537},
        {"en": "lock",        "zh": "锁",     "pic": 3261},
        {"en": "medicine",    "zh": "药",     "pic": 8163},
        {"en": "thermometer", "zh": "体温计", "pic": 32051},
        {"en": "suitcase",    "zh": "行李箱", "pic": 2931},
    ]),
    ("电器", [
        {"en": "television", "zh": "电视",   "pic": 25498},
        {"en": "radio",      "zh": "收音机", "pic": 11354, "sfx": True},
        {"en": "telephone",  "zh": "电话",   "pic": 2791,  "sfx": True},
        {"en": "camera",     "zh": "相机",   "pic": 2680},
        {"en": "fridge",     "zh": "冰箱",   "pic": 3272},
        {"en": "oven",       "zh": "烤箱",   "pic": 2426},
        {"en": "microwave",  "zh": "微波炉", "pic": 2473},
        {"en": "fan",        "zh": "电扇",   "pic": 2612},
        {"en": "printer",    "zh": "打印机", "pic": 2970},
        {"en": "iron",       "zh": "熨斗",   "pic": 2528},
        {"en": "keyboard",   "zh": "键盘",   "pic": 2793},
        {"en": "headphones", "zh": "耳机",   "pic": 5915},
    ]),
    # `mouse` pins the rodent. Search returns the computer peripheral first.
    ("农场动物", [
        {"en": "hen",     "zh": "母鸡", "pic": 2403,  "sfx": True},
        {"en": "rooster", "zh": "公鸡", "pic": 2404,  "sfx": True},
        {"en": "chick",   "zh": "小鸡", "pic": 2533,  "sfx": True},
        {"en": "goat",    "zh": "山羊", "pic": 25887, "sfx": True},
        {"en": "donkey",  "zh": "驴",   "pic": 2291,  "sfx": True},
        {"en": "rabbit",  "zh": "兔子", "pic": 2351},
        {"en": "mouse",   "zh": "老鼠", "pic": 28845, "sfx": True},
        {"en": "goose",   "zh": "鹅",   "pic": 2878,  "sfx": True},
        {"en": "turkey",  "zh": "火鸡", "pic": 2509,  "sfx": True},
        {"en": "lamb",    "zh": "羊羔", "pic": 2489,  "sfx": True},
        {"en": "pony",    "zh": "小马", "pic": 8324,  "sfx": True},
        {"en": "bull",    "zh": "公牛", "pic": 2595,  "sfx": True},
    ]),
    ("海洋动物", [
        {"en": "whale",     "zh": "鲸鱼", "pic": 2268,  "sfx": True},
        {"en": "dolphin",   "zh": "海豚", "pic": 2732,  "sfx": True},
        {"en": "shark",     "zh": "鲨鱼", "pic": 2589},
        {"en": "lobster",   "zh": "龙虾", "pic": 2947},
        {"en": "crab",      "zh": "螃蟹", "pic": 2312},
        {"en": "turtle",    "zh": "乌龟", "pic": 10241},
        {"en": "seal",      "zh": "海豹", "pic": 2397,  "sfx": True},
        {"en": "starfish",  "zh": "海星", "pic": 3310},
        {"en": "jellyfish", "zh": "水母", "pic": 2920},
        {"en": "seahorse",  "zh": "海马", "pic": 2672},
        {"en": "penguin",   "zh": "企鹅", "pic": 3243},
        {"en": "shell",     "zh": "贝壳", "pic": 34625},
    ]),
    # Insects are drawn in their real colours, and their real colours are black
    # and dark brown: ant, spider, mosquito and beetle all measured under 4%
    # lit on the black matte, i.e. invisible. White card for the unit.
    ("昆虫", [
        {"en": "bee",         "zh": "蜜蜂",   "pic": 2239,  "sfx": True, "bg": "#FFFFFF"},
        {"en": "ant",         "zh": "蚂蚁",   "pic": 2425, "bg": "#FFFFFF"},
        {"en": "butterfly",   "zh": "蝴蝶",   "pic": 2465, "bg": "#FFFFFF"},
        {"en": "spider",      "zh": "蜘蛛",   "pic": 2254, "bg": "#FFFFFF"},
        {"en": "mosquito",    "zh": "蚊子",   "pic": 2479,  "sfx": True, "bg": "#FFFFFF"},
        {"en": "ladybug",     "zh": "瓢虫",   "pic": 2924, "bg": "#FFFFFF"},
        {"en": "dragonfly",   "zh": "蜻蜓",   "pic": 9066, "bg": "#FFFFFF"},
        {"en": "grasshopper", "zh": "蚱蜢",   "pic": 2805,  "sfx": True, "bg": "#FFFFFF"},
        {"en": "beetle",      "zh": "甲虫",   "pic": 3301, "bg": "#FFFFFF"},
        {"en": "caterpillar", "zh": "毛毛虫", "pic": 16727, "bg": "#FFFFFF"},
        {"en": "worm",        "zh": "虫子",   "pic": 28485, "bg": "#FFFFFF"},
        {"en": "snail",       "zh": "蜗牛",   "pic": 2685, "bg": "#FFFFFF"},
    ]),
    ("鸟类", [
        {"en": "owl",      "zh": "猫头鹰", "pic": 2671,  "sfx": True},
        {"en": "eagle",    "zh": "老鹰",   "pic": 2638,  "sfx": True},
        {"en": "parrot",   "zh": "鹦鹉",   "pic": 2934,  "sfx": True},
        {"en": "peacock",  "zh": "孔雀",   "pic": 13348, "sfx": True},
        {"en": "swan",     "zh": "天鹅",   "pic": 2337},
        {"en": "pigeon",   "zh": "鸽子",   "pic": 25293, "sfx": True},
        {"en": "ostrich",  "zh": "鸵鸟",   "pic": 2650},
        {"en": "flamingo", "zh": "火烈鸟", "pic": 3316},
        {"en": "sparrow",  "zh": "麻雀",   "pic": 4657,  "sfx": True},
        {"en": "seagull",  "zh": "海鸥",   "pic": 3334,  "sfx": True},
        {"en": "stork",    "zh": "鹳",     "pic": 3015},
        {"en": "nest",     "zh": "鸟窝",   "pic": 7173},
    ]),
    ("野生动物", [
        {"en": "tiger",     "zh": "老虎",   "pic": 2590,  "sfx": True},
        {"en": "bear",      "zh": "熊",     "pic": 2868,  "sfx": True},
        {"en": "wolf",      "zh": "狼",     "pic": 5892,  "sfx": True},
        {"en": "fox",       "zh": "狐狸",   "pic": 2623},
        {"en": "deer",      "zh": "鹿",     "pic": 3263},
        {"en": "giraffe",   "zh": "长颈鹿", "pic": 2437},
        {"en": "zebra",     "zh": "斑马",   "pic": 2324},
        {"en": "kangaroo",  "zh": "袋鼠",   "pic": 2313},
        {"en": "panda",     "zh": "熊猫",   "pic": 2869},
        {"en": "camel",     "zh": "骆驼",   "pic": 8522},
        {"en": "hippo",     "zh": "河马",   "pic": 2424,  "sfx": True},
        {"en": "crocodile", "zh": "鳄鱼",   "pic": 2343},
    ]),
    ("小动物", [
        {"en": "snake",    "zh": "蛇",     "pic": 2568,  "sfx": True},
        {"en": "frog",     "zh": "青蛙",   "pic": 28473, "sfx": True},
        {"en": "lizard",   "zh": "蜥蜴",   "pic": 2949},
        {"en": "dinosaur", "zh": "恐龙",   "pic": 2738,  "sfx": True},
        {"en": "squirrel", "zh": "松鼠",   "pic": 2257},
        {"en": "koala",    "zh": "考拉",   "pic": 2954},
        {"en": "gorilla",  "zh": "大猩猩", "pic": 2410,  "sfx": True},
        {"en": "raccoon",  "zh": "浣熊",   "pic": 10239},
        {"en": "hedgehog", "zh": "刺猬",   "pic": 26829},
        {"en": "bat",      "zh": "蝙蝠",   "pic": 2903},
        {"en": "mole",     "zh": "鼹鼠",   "pic": 9070},
        {"en": "otter",    "zh": "水獭",   "pic": 6158},
    ]),
    ("工具", [
        {"en": "hammer", "zh": "锤子", "pic": 2922},
        {"en": "saw",    "zh": "锯子", "pic": 2800},
        {"en": "drill",  "zh": "电钻", "pic": 39587},
        {"en": "wrench", "zh": "扳手", "pic": 2937},
        {"en": "screw",  "zh": "螺丝", "pic": 2788},
        {"en": "nail",   "zh": "钉子", "pic": 2705},
        {"en": "axe",    "zh": "斧头", "pic": 6519},
        {"en": "shovel", "zh": "铁锹", "pic": 38254},
        {"en": "pliers", "zh": "钳子", "pic": 2644},
        {"en": "ladder", "zh": "梯子", "pic": 2743},
        {"en": "wheel",  "zh": "轮子", "pic": 6209},
        {"en": "magnet", "zh": "磁铁", "pic": 8138},
    ]),
    ("表情感受", [
        {"en": "happy",     "zh": "开心", "pic": 3250},
        {"en": "sad",       "zh": "伤心", "pic": 2606},
        {"en": "angry",     "zh": "生气", "pic": 2374},
        {"en": "tired",     "zh": "累",   "pic": 2314},
        {"en": "hungry",    "zh": "饿",   "pic": 4962},
        {"en": "thirsty",   "zh": "渴",   "pic": 4963},
        {"en": "scared",    "zh": "害怕", "pic": 2261},
        {"en": "surprised", "zh": "惊讶", "pic": 2574},
        {"en": "sick",      "zh": "生病", "pic": 3308},
        {"en": "shy",       "zh": "害羞", "pic": 8707},
        {"en": "excited",   "zh": "兴奋", "pic": 39090},
        {"en": "bored",     "zh": "无聊", "pic": 35531},
    ]),
]

WORDS_PER_UNIT = 12


def all_words():
    """-> [(unit_index, unit_title, word_dict), ...] in teaching order."""
    out = []
    for u, (title, words) in enumerate(UNITS):
        for w in words:
            out.append((u, title, w))
    return out


def chinese_glyphs():
    """Every Chinese character the app has to draw for this word list.

    The UI font is a subset built by tools/hanzi_pipeline; a character missing
    from that subset renders as a silent box, so the build prints this set and
    records it in the manifest.
    """
    chars = set()
    for title, words in UNITS:
        chars.update(title)
        for w in words:
            chars.update(w["zh"])
    return "".join(sorted(c for c in chars if ord(c) > 0x2E80))


def validate():
    """Cheap structural checks -- these are the mistakes that are easy to make
    when hand-editing the list above and annoying to debug on the device."""
    problems = []
    for title, words in UNITS:
        if len(words) != WORDS_PER_UNIT:
            problems.append(f"unit {title}: {len(words)} words, want {WORDS_PER_UNIT}")
        seen = set()
        for w in words:
            en = w.get("en", "")
            if not en or en != en.lower() or not en.replace(" ", "").isalpha():
                problems.append(f"unit {title}: bad headword {en!r}")
            if en in seen:
                problems.append(f"unit {title}: duplicate {en!r}")
            seen.add(en)
            if not w.get("zh"):
                problems.append(f"{en}: missing Chinese gloss")
            elif len(w["zh"]) > 4:
                problems.append(f"{en}: gloss {w['zh']!r} is longer than 4 chars")
            bg = w.get("bg")
            if bg is not None and (len(bg) != 7 or bg[0] != "#"):
                problems.append(f"{en}: bad bg {bg!r}, want #RRGGBB")
    return problems


if __name__ == "__main__":
    bad = validate()
    for line in bad:
        print("PROBLEM:", line)
    words = all_words()
    print(f"{len(UNITS)} units, {len(words)} words, "
          f"{sum(1 for _, _, w in words if w.get('sfx'))} marked for sfx")
    print("chinese glyphs:", chinese_glyphs())
    raise SystemExit(1 if bad else 0)
