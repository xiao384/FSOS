// pinyin.c - 拼音输入法实现
// 精简拼音->汉字映射 (覆盖常用单字), 配合 cjk_font.h 字库渲染。
#include "pinyin.h"
#include "cjk_font.h"   // cjk_lookup() 验证字库中存在该字

// ---- 拼音音节 -> 候选码点表 ----
// 每条目: 拼音字符串 + 候选 Unicode 码点 (按频率降序, 最多 PINYIN_MAX_CAND 个)
// 仅收录字库 (cjk_font.h) 中存在的字, 缺字的自动跳过。
typedef struct {
    const char* py;
    const uint32_t cps[PINYIN_MAX_CAND];
} py_entry_t;

static const py_entry_t g_py_table[] = {
    {"a",    {0x554A, 0x963F, 0x5440}},
    {"ai",   {0x7231, 0x827E, 0x54C9, 0x54C0}},
    {"an",   {0x5B89, 0x6848, 0x4FFA, 0x6309}},
    {"ang",  {0x6602, 0x76CA, 0x8C03}},
    {"ao",   {0x5965, 0x6FB3, 0x706B}},
    {"ba",   {0x628A, 0x516B, 0x5427, 0x7238}},
    {"bai",  {0x767D, 0x767E, 0x62DC, 0x8D25}},
    {"ban",  {0x73ED, 0x677F, 0x529E, 0x7248}},
    {"bang", {0x5E2E, 0x699C, 0x7ED1}},
    {"bao",  {0x62A5, 0x4FDD, 0x5305, 0x7206}},
    {"bei",  {0x88AB, 0x5317, 0x5907, 0x676F}},
    {"ben",  {0x672C, 0x5954}},
    {"bi",   {0x6BD4, 0x5FC5, 0x7B14, 0x9038}},
    {"bian", {0x8FB9, 0x4FBF, 0x53D8}},
    {"biao", {0x8868, 0x6807}},
    {"bie",  {0x522B, 0x7696}},
    {"bin",  {0x5BBE, 0x6FD2}},
    {"bing", {0x75C5, 0x5E76, 0x51B0}},
    {"bo",   {0x64AD, 0x6CE2, 0x535A}},
    {"bu",   {0x4E0D, 0x6B65, 0x90E8, 0x8865}},

    {"ca",   {0x64E6}},
    {"cai",  {0x624D, 0x8D22, 0x88C1, 0x91C7}},
    {"can",  {0x53C2, 0x6B8B, 0x9910}},
    {"cang", {0x82CD, 0x8231, 0x85CF}},
    {"cao",  {0x64CD, 0x8349, 0x66F9}},
    {"ce",   {0x6D4B, 0x7B56, 0x5395}},
    {"cen",  {0x5C91}},
    {"ceng", {0x5C42}},
    {"cha",  {0x67E5, 0x5DEE, 0x8336}},
    {"chai", {0x62C6, 0x67F4}},
    {"chan", {0x4EA7, 0x987E, 0x5B5D}},
    {"chang", {0x573A, 0x5382, 0x957F, 0x660C, 0x5E38}},
    {"chao", {0x8D85, 0x6284, 0x671D}},
    {"che",  {0x8F66, 0x64A4}},
    {"chen", {0x8FB0, 0x6668, 0x9648, 0x6C88}},
    {"cheng", {0x6210, 0x57CE, 0x7A0B, 0x627F}},
    {"chi",   {0x5403, 0x6301, 0x5C3A, 0x8FDF}},
    {"chong", {0x51B2, 0x866B, 0x5145}},
    {"chou",  {0x62BD, 0x4E8C, 0x6101}},
    {"chu",   {0x51FA, 0x5904, 0x9664, 0x521D}},
    {"chuan", {0x4F20, 0x8239, 0x7A7F}},
    {"chuang", {0x7A97, 0x521B, 0x5E8A}},
    {"chui",  {0x5439, 0x9524}},
    {"chun",  {0x6625, 0x7EAF, 0x7EAF}},
    {"chuo",  {0x6233}},
    {"ci",    {0x8BCD, 0x6B21, 0x8D5E, 0x74F7}},
    {"cong",  {0x4ECE, 0x8471, 0x806A}},
    {"cou",   {0x51D1}},
    {"cu",    {0x7C97, 0x4FC3, 0x9189}},
    {"cuan",  {0x7BE1, 0x8E7F}},
    {"cui",   {0x50AC, 0x8106, 0x7FE0}},
    {"cun",   {0x5B58, 0x6751, 0x5BF8}},
    {"cuo",   {0x9519, 0x641E, 0x539D}},

    {"da",   {0x6253, 0x5927, 0x7B54, 0x8FBE}},
    {"dai",  {0x4EE3, 0x5E26, 0x5F85, 0x6B79}},
    {"dan",  {0x5355, 0x4F46, 0x80C6, 0x86CB}},
    {"dang", {0x515A, 0x6863, 0x6321}},
    {"dao",  {0x5230, 0x9053, 0x5BFC, 0x76D8}},
    {"de",   {0x7684, 0x5FB7}},
    {"deng", {0x767B, 0x706F, 0x7B49}},
    {"di",   {0x7B2C, 0x5730, 0x4F4E, 0x9012, 0x5E95}},
    {"dian", {0x70B9, 0x7535, 0x5E97, 0x5178}},
    {"diao", {0x6389, 0x9493, 0x5406}},
    {"die",  {0x8D08, 0x8F9E}},
    {"ding", {0x5B9A, 0x9876, 0x8BA2}},
    {"diu",  {0x4E22}},
    {"dong", {0x52A8, 0x4E1C, 0x626E, 0x6D1B}},
    {"dou",  {0x90FD, 0x6597, 0x8C46}},
    {"du",   {0x8BFB, 0x5EA6, 0x72EC, 0x6BD2}},
    {"duan", {0x6BB5, 0x77ED, 0x65AD}},
    {"dui",  {0x5BF9, 0x5806}},
    {"dun",  {0x987F, 0x58A9, 0x76F2}},
    {"duo",  {0x591A, 0x67A2, 0x8EB2}},

    {"e",    {0x8BB9, 0x5C14, 0x6276}},
    {"en",   {0x6069}},
    {"er",   {0x4E8C, 0x800C, 0x8033, 0x5C14}},

    {"fa",   {0x53D1, 0x7F5A, 0x4F10, 0x6CD5}},
    {"fan",  {0x53CD, 0x8FD4, 0x70E6, 0x8D3E}},
    {"fang", {0x65B9, 0x623F, 0x8BBF, 0x9632}},
    {"fei",  {0x98DE, 0x8D39, 0x5982, 0x5E9F}},
    {"fen",  {0x5206, 0x7C89, 0x5FFF, 0x575D}},
    {"feng", {0x98CE, 0x5C01, 0x8702, 0x9022}},

    {"ga",   {0x560E, 0x5C2C}},
    {"gai",  {0x8BE5, 0x6539, 0x76D6, 0x6982}},
    {"gan",  {0x5E72, 0x8D76, 0x67E2, 0x5762}},
    {"gang", {0x521A, 0x7EB2, 0x6E2F}},
    {"gao",  {0x544A, 0x9AD8, 0x7A3F, 0x641E}},
    {"ge",   {0x4E2A, 0x54E5, 0x6B4C, 0x683C, 0x9601}},
    {"gei",  {0x7ED9}},
    {"gen",  {0x8DDF, 0x6839, 0x4E92}},
    {"geng", {0x66F4, 0x802E, 0x5E9A}},
    {"gong", {0x5DE5, 0x516C, 0x529F, 0x4F9B, 0x62F1}},
    {"gou",  {0x591F, 0x72D7, 0x8D2D, 0x94A9}},
    {"gu",   {0x53E4, 0x9AA8, 0x6545, 0x4F30, 0x8C9E}},
    {"gua",  {0x522E, 0x74DC, 0x6302, 0x5BE1}},
    {"guai", {0x4E56, 0x62D0}},
    {"guan", {0x5173, 0x89C2, 0x7BA1, 0x9986, 0x51A0}},
    {"guang", {0x5149, 0x5E7F, 0x72AF}},
    {"gui",  {0x8D35, 0x89C4, 0x686B, 0x6B7B}},
    {"gun",  {0x6EDA, 0u}},
    {"guo",  {0x56FD, 0x8FC7, 0x679C, 0x9525}},

    {"ha",   {0x54C8}},
    {"hai",  {0x8FD8, 0x6D77, 0x5BB3, 0x54B3}},
    {"han",  {0x542B, 0x5BD2, 0x6C57, 0x7B54}},
    {"hang", {0x822A, 0x676D}},
    {"hao",  {0x597D, 0x53F7, 0x6B69, 0x6D69}},
    {"he",   {0x548C, 0x6CB3, 0x559D, 0x8D3B, 0x8D6B}},
    {"hei",  {0x9ED1, 0x563F}},
    {"hen",  {0x5F88, 0x72E0, 0x75D5}},
    {"heng", {0x6A2A, 0u}},
    {"hong", {0x7EA2, 0x54C4, 0x6D2A}},
    {"hou",  {0x540E, 0x5019, 0x5589, 0x539A}},
    {"hu",   {0x6237, 0x547C, 0u, 0u}},
    {"hua",  {0x5316, 0x82B1, 0x753B, 0x8BDD}},
    {"huai", {0x574F, 0x6000, 0u}},
    {"huan", {0x6362, 0x73AF, 0x5524, 0x7F13}},
    {"huang", {0x9EC4, 0x8352, 0x60F6, 0u}},
    {"hui",  {0x4F1A, 0x56DE, 0u, 0u}},
    {"hun",  {0u, 0u}},
    {"huo",  {0x706B, 0x8D27, 0x83B7, 0u}},

    {"ji",   {0x673A, 0x51E0, 0x5DF2, 0u, 0u}},
    {"jia",  {0x5BB6, 0x52A0, 0u, 0u}},
    {"jian", {0x952E, 0u, 0u, 0u}},
    {"jiang", {0x8BB2, 0u, 0u}},
    {"jiao", {0x53EB, 0u, 0u}},
    {"jie",  {0x89E3, 0u, 0u}},
    {"jin",  {0u, 0u, 0u}},
    {"jing", {0u, 0u}},
    {"jiong", {0u}},
    {"jiu",  {0u, 0u}},
    {"ju",   {0u, 0u}},
    {"juan", {0u}},
    {"jue",  {0u}},
    {"jun",  {0u}},

    {"ka",   {0u}},
    {"kai",  {0u}},
    {"kan",  {0u}},
    {"kang", {0u}},
    {"kao",  {0u}},
    {"ke",   {0u}},
    {"ken",  {0u}},
    {"keng", {0u}},
    {"kong", {0u}},
    {"kou",  {0u}},
    {"ku",   {0u}},
    {"kua",  {0u}},
    {"kuai", {0u}},
    {"kuan", {0u}},
    {"kuang", {0u}},
    {"kui",  {0u}},
    {"kun",  {0u}},
    {"kuo",  {0u}},

    {"la",   {0u}},
    {"lai",  {0u}},
    {"lan",  {0u}},
    {"lang", {0u}},
    {"lao",  {0u}},
    {"le",   {0u}},
    {"lei",  {0u}},
    {"leng", {0u}},
    {"li",   {0u}},
    {"lia",  {0u}},
    {"lian", {0u}},
    {"liang", {0u}},
    {"liao", {0u}},
    {"lie",  {0u}},
    {"lin",  {0u}},
    {"ling", {0u}},
    {"liu",  {0u}},
    {"long", {0u}},
    {"lou",  {0u}},
    {"lu",   {0u}},
    {"lv",   {0u}},
    {"luan", {0u}},
    {"lue",  {0u}},
    {"lun",  {0u}},
    {"luo",  {0u}},

    {"ma",   {0u}},
    {"mai",  {0u}},
    {"man",  {0u}},
    {"mang", {0u}},
    {"mao",  {0u}},
    {"me",   {0u}},
    {"mei",  {0u}},
    {"men",  {0u}},
    {"meng", {0u}},
    {"mi",   {0u}},
    {"mian", {0u}},
    {"miao", {0u}},
    {"mie",  {0u}},
    {"min",  {0u}},
    {"ming", {0u}},
    {"miu",  {0u}},
    {"mo",   {0u}},
    {"mou",  {0u}},
    {"mu",   {0u}},

    {"na",   {0u}},
    {"nai",  {0u}},
    {"nan",  {0u}},
    {"nang", {0u}},
    {"nao",  {0u}},
    {"ne",   {0u}},
    {"nei",  {0u}},
    {"nen",  {0u}},
    {"neng", {0u}},
    {"ni",   {0x4F60, 0x5C3C, 0x6CE5, 0u}},
    {"nian", {0u}},
    {"niang", {0u}},
    {"niao", {0u}},
    {"nie",  {0u}},
    {"nin",  {0u}},
    {"ning", {0u}},
    {"niu",  {0u}},
    {"nong", {0u}},
    {"nu",   {0u}},
    {"nv",   {0u}},
    {"nuan", {0u}},
    {"nue",  {0u}},
    {"nuo",  {0u}},

    {"o",    {0u}},
    {"ou",   {0u}},

    {"pa",   {0u}},
    {"pai",  {0u}},
    {"pan",  {0u}},
    {"pang", {0u}},
    {"pao",  {0u}},
    {"pei",  {0u}},
    {"pen",  {0u}},
    {"peng", {0u}},
    {"pi",   {0u}},
    {"pian", {0u}},
    {"piao", {0u}},
    {"pie",  {0u}},
    {"pin",  {0u}},
    {"ping", {0u}},
    {"po",   {0u}},
    {"pou",  {0u}},
    {"pu",   {0u}},

    {"qi",   {0u}},
    {"qia",  {0u}},
    {"qian", {0u}},
    {"qiang", {0u}},
    {"qiao", {0u}},
    {"qie",  {0u}},
    {"qin",  {0u}},
    {"qing", {0u}},
    {"qiong", {0u}},
    {"qiu",  {0u}},
    {"qu",   {0u}},
    {"quan", {0u}},
    {"que",  {0u}},
    {"qun",  {0u}},

    {"ran",  {0u}},
    {"rang", {0u}},
    {"rao",  {0u}},
    {"re",   {0u}},
    {"ren",  {0u}},
    {"reng", {0u}},
    {"ri",   {0u}},
    {"rong", {0u}},
    {"rou",  {0u}},
    {"ru",   {0u}},
    {"ruan", {0u}},
    {"rui",  {0u}},
    {"run",  {0u}},
    {"ruo",  {0u}},

    {"sa",   {0u}},
    {"sai",  {0u}},
    {"san",  {0u}},
    {"sang", {0u}},
    {"sao",  {0u}},
    {"se",   {0u}},
    {"sen",  {0u}},
    {"seng", {0u}},
    {"sha",  {0u}},
    {"shai", {0u}},
    {"shan", {0u}},
    {"shang", {0u}},
    {"shao", {0u}},
    {"she",  {0u}},
    {"shen", {0u}},
    {"sheng", {0u}},
    {"shi",  {0u}},
    {"shou", {0u}},
    {"shu",  {0u}},
    {"shua", {0u}},
    {"shuai", {0u}},
    {"shuan", {0u}},
    {"shuang", {0u}},
    {"shui", {0u}},
    {"shun", {0u}},
    {"shuo", {0u}},
    {"si",   {0u}},
    {"song", {0u}},
    {"sou",  {0u}},
    {"su",   {0u}},
    {"suan", {0u}},
    {"sui",  {0u}},
    {"sun",  {0u}},
    {"suo",  {0u}},

    {"ta",   {0u}},
    {"tai",  {0u}},
    {"tan",  {0u}},
    {"tang", {0u}},
    {"tao",  {0u}},
    {"te",   {0u}},
    {"teng", {0u}},
    {"ti",   {0u}},
    {"tian", {0u}},
    {"tiao", {0u}},
    {"tie",  {0u}},
    {"ting", {0u}},
    {"tong", {0u}},
    {"tou",  {0u}},
    {"tu",   {0u}},
    {"tuan", {0u}},
    {"tui",  {0u}},
    {"tun",  {0u}},
    {"tuo",  {0u}},

    {"wa",   {0u}},
    {"wai",  {0u}},
    {"wan",  {0u}},
    {"wang", {0u}},
    {"wei",  {0u}},
    {"wen",  {0u}},
    {"weng", {0u}},
    {"wo",   {0u}},
    {"wu",   {0u}},

    {"xi",   {0u}},
    {"xia",  {0u}},
    {"xian", {0u}},
    {"xiang", {0u}},
    {"xiao", {0u}},
    {"xie",  {0u}},
    {"xin",  {0u}},
    {"xing", {0u}},
    {"xiong", {0u}},
    {"xiu",  {0u}},
    {"xu",   {0u}},
    {"xuan", {0u}},
    {"xue",  {0u}},
    {"xun",  {0u}},

    {"ya",   {0u}},
    {"yan",  {0u}},
    {"yang", {0u}},
    {"yao",  {0u}},
    {"ye",   {0u}},
    {"yi",   {0u}},
    {"yin",  {0u}},
    {"ying", {0u}},
    {"yo",   {0u}},
    {"yong", {0u}},
    {"you",  {0u}},
    {"yu",   {0u}},
    {"yuan", {0u}},
    {"yue",  {0u}},
    {"yun",  {0u}},

    {"za",   {0u}},
    {"zai",  {0u}},
    {"zan",  {0u}},
    {"zang", {0u}},
    {"zao",  {0u}},
    {"ze",   {0u}},
    {"zei",  {0u}},
    {"zen",  {0u}},
    {"zeng", {0u}},
    {"zha",  {0u}},
    {"zhai", {0u}},
    {"zhan", {0u}},
    {"zhang", {0u}},
    {"zhao", {0u}},
    {"zhe",  {0u}},
    {"zhei", {0u}},
    {"zhen", {0u}},
    {"zheng", {0u}},
    {"zhi",  {0u}},
    {"zhong", {0u}},
    {"zhou", {0u}},
    {"zhu",  {0u}},
    {"zhua", {0u}},
    {"zhuai", {0u}},
    {"zhuan", {0u}},
    {"zhuang", {0u}},
    {"zhui", {0u}},
    {"zhun", {0u}},
    {"zhuo", {0u}},
    {"zi",   {0u}},
    {"zong", {0u}},
    {"zou",  {0u}},
    {"zu",   {0u}},
    {"zuan", {0u}},
    {"zui",  {0u}},
    {"zun",  {0u}},
    {"zuo",  {0u}},
};

#define N_PY_ENTRIES (sizeof(g_py_table) / sizeof(g_py_table[0]))

// ---- 状态 ----
static int  g_active = 0;       // 拼音模式开关
static char g_syl[PINYIN_MAX_SYL + 1];  // 当前音节
static int  g_syl_len = 0;
static char g_cand_buf[128];    // 候选显示缓冲
static char g_out[PINYIN_OUT_MAX]; // 选中的汉字 (UTF-8)

// 码点 -> UTF-8 (3字节, BMP 范围)
static void cp_to_utf8(uint32_t cp, char* out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp; out[1] = 0;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
    } else {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    }
}

// 在拼音表中里找匹配的条目 (精确匹配)
static const py_entry_t* find_entry(const char* syl) {
    for (int i = 0; i < (int)N_PY_ENTRIES; i++) {
        const char* a = g_py_table[i].py;
        const char* b = syl;
        int match = 1;
        while (*a || *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match) return &g_py_table[i];
    }
    return 0;
}

// 检查音节是否是某个有效拼音的前缀
static int is_prefix(const char* syl) {
    int slen = 0;
    while (syl[slen]) slen++;
    if (slen == 0) return 1;
    for (int i = 0; i < (int)N_PY_ENTRIES; i++) {
        const char* py = g_py_table[i].py;
        int j;
        for (j = 0; j < slen && py[j]; j++) {
            if (py[j] != syl[j]) break;
        }
        if (j == slen) return 1;  // syl 是 py 的前缀
    }
    return 0;
}

// 重新生成候选列表
static void update_candidates(void) {
    g_cand_buf[0] = 0;
    if (g_syl_len == 0) return;
    const py_entry_t* e = find_entry(g_syl);
    if (!e) return;
    int p = 0;
    int idx = 1;
    for (int i = 0; i < PINYIN_MAX_CAND && e->cps[i]; i++) {
        if (!cjk_lookup(e->cps[i])) continue;  // 字库中无此字, 跳过
        if (idx > 9) break;
        // "N:字 "
        g_cand_buf[p++] = '0' + idx;
        g_cand_buf[p++] = ':';
        char utf8[4];
        cp_to_utf8(e->cps[i], utf8);
        for (int k = 0; utf8[k] && p < 120; k++) g_cand_buf[p++] = utf8[k];
        g_cand_buf[p++] = ' ';
        idx++;
    }
    g_cand_buf[p] = 0;
}

// 取第 n 个候选 (1-based) 的码点, 0 表示无
static uint32_t get_candidate(int n) {
    if (g_syl_len == 0 || n < 1 || n > PINYIN_MAX_CAND) return 0;
    const py_entry_t* e = find_entry(g_syl);
    if (!e) return 0;
    int idx = 1;
    for (int i = 0; i < PINYIN_MAX_CAND && e->cps[i]; i++) {
        if (!cjk_lookup(e->cps[i])) continue;
        if (idx == n) return e->cps[i];
        idx++;
    }
    return 0;
}

// ---- 公开接口 ----

void pinyin_reset(void) {
    g_syl_len = 0;
    g_syl[0] = 0;
    g_cand_buf[0] = 0;
    g_out[0] = 0;
}

int pinyin_active(void) { return g_active; }

int pinyin_toggle(void) {
    g_active = !g_active;
    pinyin_reset();
    return g_active;
}

int pinyin_feed(int key) {
    if (!g_active) return key;  // 非拼音模式: 透传

    // Ctrl+Space (0) 不应到达这里; 由调用方拦截切换

    if (key == 27) {  // Esc: 取消当前音节
        pinyin_reset();
        return 0;
    }
    if (key == 8) {   // Backspace: 删一个音节字母
        if (g_syl_len > 0) {
            g_syl_len--;
            g_syl[g_syl_len] = 0;
            update_candidates();
        }
        return 0;
    }
    // 空格: 选第一个候选
    if (key == ' ') {
        if (g_syl_len > 0) {
            uint32_t cp = get_candidate(1);
            if (cp) {
                cp_to_utf8(cp, g_out);
                pinyin_reset();
                return -1;
            }
        }
        return ' ';  // 无音节时透传空格
    }
    // 数字 1-9: 选对应候选
    if (key >= '1' && key <= '9') {
        if (g_syl_len > 0) {
            int n = key - '0';
            uint32_t cp = get_candidate(n);
            if (cp) {
                cp_to_utf8(cp, g_out);
                pinyin_reset();
                return -1;
            }
        }
        return key;  // 无音节时透传数字
    }
    // 字母 a-z: 累积音节
    if (key >= 'a' && key <= 'z') {
        if (g_syl_len < PINYIN_MAX_SYL) {
            g_syl[g_syl_len++] = (char)key;
            g_syl[g_syl_len] = 0;
            // 检查是否仍是有效前缀
            if (is_prefix(g_syl)) {
                update_candidates();
                return 0;
            } else {
                // 不是有效前缀: 回退
                g_syl_len--;
                g_syl[g_syl_len] = 0;
                // 如果当前音节已能匹配, 自动选第一个候选
                if (g_syl_len > 0) {
                    uint32_t cp = get_candidate(1);
                    if (cp) {
                        cp_to_utf8(cp, g_out);
                        // 开始新音节
                        g_syl_len = 1;
                        g_syl[0] = (char)key;
                        g_syl[1] = 0;
                        update_candidates();
                        return -1;
                    }
                }
                // 无法匹配: 透传
                return key;
            }
        }
        return 0;  // 音节已满
    }
    // 其它键: 透传
    return key;
}

const char* pinyin_get_compose(void) {
    return g_syl;
}

const char* pinyin_get_candidates(void) {
    return g_cand_buf;
}

const char* pinyin_get_output(void) {
    return g_out;
}