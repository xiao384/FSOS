// theme.c - FSOS 运行时 Theme API 实现 (gui_framework Phase 2)
//
// 深色/浅色主题令牌表 + LFB 32bpp 真彩与 8bpp 调色板索引降级。
// 设计依据: .codeartsdoer/specs/gui_framework/design.md 2.6
//
// 默认深色主题 (spec 5.1.2): 深蓝/蓝黑背景 + 蓝色强调 + 高对比浅色文字。
// 8bpp 降级复用 COL_WALL_F/COL_UI_*/COL_ACCENT 等既有色板。
#include "theme_api.h"
#include "vga.h"    // COL_* 调色板索引

// ---- 深色主题 LFB 真彩令牌表 (0x00RRGGBB) ----
// 对齐概念图基准: 面板 #1e1e2e / 强调 #4a9eff / 槽灰 #3a3a4e
static const uint32_t dark_colors_lfb[COLOR_COUNT] = {
    [COLOR_BG]          = 0x00111118,  // 深色桌面 (比面板略深)
    [COLOR_BG_PANEL]    = 0x001e1e2e,  // 面板/对话框 (概念图基准)
    [COLOR_BG_TITLE]    = 0x00161626,  // 标题栏 (面板与输入框之间)
    [COLOR_BG_TASKBAR]  = 0x00111122,  // 半透明感任务栏底色
    [COLOR_BG_MENU]     = 0x001e1e2e,  // 开始菜单/弹出层 (与面板一致)
    [COLOR_FG]          = 0x00F5F7FA,  // 主文字
    [COLOR_FG_SOFT]     = 0x0099A9BE,  // 次要文字
    [COLOR_FG_TITLE]    = 0x00FFFFFF,  // 标题文字
    [COLOR_ACCENT]      = 0x004a9eff,  // 强调蓝 (概念图基准)
    [COLOR_ACCENT_SOFT] = 0x003a7ec0,  // 柔和强调
    [COLOR_BORDER]      = 0x00303046,  // 边界 (概念图基准)
    [COLOR_BORDER_FOCUS]= 0x006bb5ff,  // 聚焦边界 (强调亮化)
    [COLOR_SHADOW]      = 0x00070710,  // 阴影
    [COLOR_HOVER]       = 0x003a3a4e,  // 悬停层 (概念图槽灰)
    [COLOR_PRESSED]     = 0x002d6dbf,  // 按下层
    [COLOR_DISABLED]    = 0x00252533,  // 禁用层
    [COLOR_SUCCESS]     = 0x004CAF50,  // 成功绿 (在线色, 概念图 #4caf50)
    [COLOR_WARNING]     = 0x00FF9800,  // 警告橙
    [COLOR_DANGER]      = 0x00F44336,  // 危险红
    [COLOR_FIELD]       = 0x00161626,  // 输入框背景 (概念图基准)
    [COLOR_FIELD_FOCUS] = 0x00202030,  // 输入框聚焦
};

// ---- 深色主题 8bpp 调色板索引降级表 ----
// 复用既有 COL_* (0..31) + COL_UI_* (160..165) 色板。
static const uint8_t dark_colors_8bpp[COLOR_COUNT] = {
    [COLOR_BG]          = COL_WALL_F,          // 26 最深壁纸
    [COLOR_BG_PANEL]    = COL_UI_BG_SOFT,      // 163 夜色蓝灰
    [COLOR_BG_TITLE]    = COL_UI_TITLE_SOFT,   // 164 标题栏深蓝
    [COLOR_BG_TASKBAR]  = COL_TASKBAR,         // 29 浅色玻璃替代
    [COLOR_BG_MENU]     = COL_PANEL,           // 16 面板浅蓝灰
    [COLOR_FG]          = COL_WHITE,           // 15
    [COLOR_FG_SOFT]     = COL_LGRAY,           // 7
    [COLOR_FG_TITLE]    = COL_WHITE,           // 15
    [COLOR_ACCENT]      = COL_ACCENT,          // 27 强调蓝
    [COLOR_ACCENT_SOFT] = COL_ACCENT_SOFT,     // 28 柔和蓝
    [COLOR_BORDER]      = COL_SHADOW,          // 31
    [COLOR_BORDER_FOCUS]= COL_ACCENT,          // 27
    [COLOR_SHADOW]      = COL_BLACK,           // 0
    [COLOR_HOVER]       = COL_TASK_HI,         // 30 悬停
    [COLOR_PRESSED]     = COL_ACCENT,          // 27
    [COLOR_DISABLED]    = COL_DGRAY,           // 8
    [COLOR_SUCCESS]     = COL_LGREEN,          // 10
    [COLOR_WARNING]     = COL_ORANGE,          // 19
    [COLOR_DANGER]      = COL_LRED,            // 12
    [COLOR_FIELD]       = COL_FIELD,           // 20
    [COLOR_FIELD_FOCUS] = COL_PANEL_HI,        // 17
};

// ---- 浅色主题 LFB 真彩令牌表 (备用, 默认不激活) ----
static const uint32_t light_colors_lfb[COLOR_COUNT] = {
    [COLOR_BG]          = 0x00F3F5F8,  // 浅灰桌面
    [COLOR_BG_PANEL]    = 0x00FFFFFF,  // 白色面板
    [COLOR_BG_TITLE]    = 0x00E3E8EF,  // 浅标题栏
    [COLOR_BG_TASKBAR]  = 0x00EDF1F5,  // 浅任务栏
    [COLOR_BG_MENU]     = 0x00FFFFFF,  // 白色菜单
    [COLOR_FG]          = 0x001A2B3C,  // 深色文字
    [COLOR_FG_SOFT]     = 0x005A6B7C,  // 次要文字
    [COLOR_FG_TITLE]    = 0x001A2B3C,  // 标题文字
    [COLOR_ACCENT]      = 0x002B88D8,  // 蓝色强调 (与深色一致)
    [COLOR_ACCENT_SOFT] = 0x006FB5E8,  // 柔和强调
    [COLOR_BORDER]      = 0x00C8D0D8,  // 边框
    [COLOR_BORDER_FOCUS]= 0x002B88D8,  // 聚焦边框
    [COLOR_SHADOW]      = 0x00808080,  // 阴影
    [COLOR_HOVER]       = 0x00E3E8EF,  // 悬停
    [COLOR_PRESSED]     = 0x00C8D0D8,  // 按下
    [COLOR_DISABLED]    = 0x00EDF1F5,  // 禁用
    [COLOR_SUCCESS]     = 0x004CAF50,  // 成功
    [COLOR_WARNING]     = 0x00FF9800,  // 警告
    [COLOR_DANGER]      = 0x00F44336,  // 危险
    [COLOR_FIELD]       = 0x00FFFFFF,  // 输入框
    [COLOR_FIELD_FOCUS] = 0x00F3F5F8,  // 输入框聚焦
};

// ---- 浅色主题 8bpp 调色板索引降级表 ----
static const uint8_t light_colors_8bpp[COLOR_COUNT] = {
    [COLOR_BG]          = COL_TASKBAR,         // 29 浅色玻璃
    [COLOR_BG_PANEL]    = COL_WHITE,           // 15
    [COLOR_BG_TITLE]    = COL_PANEL,           // 16
    [COLOR_BG_TASKBAR]  = COL_TASKBAR,         // 29
    [COLOR_BG_MENU]     = COL_WHITE,           // 15
    [COLOR_FG]          = COL_BLACK,           // 0
    [COLOR_FG_SOFT]     = COL_DGRAY,           // 8
    [COLOR_FG_TITLE]    = COL_BLACK,           // 0
    [COLOR_ACCENT]      = COL_ACCENT,          // 27
    [COLOR_ACCENT_SOFT] = COL_ACCENT_SOFT,     // 28
    [COLOR_BORDER]      = COL_LGRAY,           // 7
    [COLOR_BORDER_FOCUS]= COL_ACCENT,          // 27
    [COLOR_SHADOW]      = COL_DGRAY,           // 8
    [COLOR_HOVER]       = COL_PANEL,           // 16
    [COLOR_PRESSED]     = COL_PANEL_HI,        // 17
    [COLOR_DISABLED]    = COL_LGRAY,           // 7
    [COLOR_SUCCESS]     = COL_LGREEN,          // 10
    [COLOR_WARNING]     = COL_ORANGE,          // 19
    [COLOR_DANGER]      = COL_LRED,            // 12
    [COLOR_FIELD]       = COL_WHITE,           // 15
    [COLOR_FIELD_FOCUS] = COL_FIELD,           // 20
};

// ---- 内部静态状态 ----
static theme_mode_t g_theme_mode = THEME_MODE_DARK;
static int          g_is_lfb     = 0;
static int          g_scale      = 1;
static int          g_sf         = 1;   // 主题令牌均为逻辑像素；原生放大由 gfx_scale 负责

// ---- 字体规格表 (基础值, 不随主题变, 随 scale 缩放) ----
// [font_token_t] = { width, height }
static const int font_specs[FONT_TOKEN_COUNT][2] = {
    [FONT_TOKEN_CJK]   = { 24, 24 },  // 1920x1080 UI 中文
    [FONT_TOKEN_ASCII] = { 16, 16 },  // 1920x1080 UI ASCII
    [FONT_TOKEN_TITLE] = { 16, 24 },  // 标题
    [FONT_TOKEN_BODY]  = { 16, 24 },  // 正文
    [FONT_TOKEN_SMALL] = { 12, 16 },  // 小号/状态栏
};

// ==================== 接口实现 ====================

void theme_init(int is_lfb, int scale) {
    g_is_lfb = is_lfb ? 1 : 0;
    g_scale  = (scale >= 1) ? scale : 1;
    g_sf     = (g_is_lfb ? 1 : ((scale >= 3) ? 2 : 1));
    g_theme_mode = THEME_MODE_DARK;  // 默认深色
}

uint32_t theme_get_color(color_token_t id) {
    if (id < 0 || id >= COLOR_COUNT) return 0;
    if (g_is_lfb) {
        return (g_theme_mode == THEME_MODE_DARK)
            ? dark_colors_lfb[id]
            : light_colors_lfb[id];
    }
    // 8bpp: 返回调色板索引 (扩展为 uint32_t 便于统一处理)
    return (g_theme_mode == THEME_MODE_DARK)
        ? (uint32_t)dark_colors_8bpp[id]
        : (uint32_t)light_colors_8bpp[id];
}

uint8_t theme_get_color_idx(color_token_t id) {
    if (id < 0 || id >= COLOR_COUNT) return 0;
    /* Native GOP uses the extended palette namespace as an 8-bit RGB token.
       8bpp keeps the legacy VGA palette mapping unchanged. */
    if (g_is_lfb) return (uint8_t)(COL_UI_BASE + id);
    return (g_theme_mode == THEME_MODE_DARK) ? dark_colors_8bpp[id] : light_colors_8bpp[id];
}

uint32_t theme_get_accent(void) {
    return theme_get_color(COLOR_ACCENT);
}

int theme_get_font_w(font_token_t id) {
    if (id < 0 || id >= FONT_TOKEN_COUNT) return 8;
    return font_specs[id][0];
}

int theme_get_scale_factor(void) { return g_sf; }

int theme_get_font_h(font_token_t id) {
    if (id < 0 || id >= FONT_TOKEN_COUNT) return 8;
    return font_specs[id][1];
}

int theme_get_radius(radius_token_t id) {
    // 圆角基础值对齐概念图: 面板/窗口 12px, 控件 6px, 菜单 12px
    int base;
    switch (id) {
        case RADIUS_WINDOW_T: base = 12; break;
        case RADIUS_PANEL_T:  base = 12; break;
        case RADIUS_CTRL_T:   base = 6;  break;
        case RADIUS_MENU_T:   base = 12; break;
        default:              base = 3;  break;
    }
    return base * g_sf;
}

int theme_get_padding(padding_token_t id) {
    // 间距对齐概念图: 面板内边距 16px, 区块间距 14px
    int base;
    switch (id) {
        case PADDING_UNIT: base = 2;  break;
        case PADDING_SM:   base = 6;  break;
        case PADDING_MD:   base = 14; break;
        case PADDING_LG:   base = 18; break;
        case PADDING_CTRL: base = 6;  break;
        case PADDING_PANEL:base = 16; break;
        default:           base = 2;  break;
    }
    return base * g_sf;
}

int theme_get_shadow_off(void) {
    return 2 * g_sf;  // SHADOW_OFF
}

uint8_t theme_get_shadow_col(void) {
    return theme_get_color_idx(COLOR_SHADOW);
}

int theme_get_shadow_intensity(int focused) {
    return focused ? 1 : 0;  // SHADOW_FOCUS=1, SHADOW_NORM=0
}

int theme_get_anim_duration(anim_token_t id) {
    // 现代桌面默认启用短动画；可由 Animation System 整体关闭。
    switch (id) {
        case ANIM_MENU_T:   return 180;
        case ANIM_WINDOW_T: return 160;
        case ANIM_FADE_T:   return 140;
        default:            return 160;
    }
}

int theme_get_blur_radius(void) {
    // LFB 真彩且性能档允许 (scale>=2) 时启用毛玻璃模糊, 否则降级为半透明纯色
    if (g_is_lfb && g_scale >= 2) return 8;
    return 0;
}

void theme_set_mode(theme_mode_t mode) {
    if (mode == g_theme_mode) return;
    g_theme_mode = mode;
    // 主题切换后需标记全屏脏, 由 compositor 处理 (Phase 3 接入)
}

theme_mode_t theme_get_mode(void) {
    return g_theme_mode;
}