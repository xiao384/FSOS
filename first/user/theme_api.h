// theme_api.h - FSOS 运行时 Theme API (gui_framework Phase 2)
//
// 替代 theme.h 编译期宏的运行时设计令牌查询接口。支持深色/浅色主题切换、
// LFB 32bpp 真彩与 8bpp 调色板索引降级、分辨率自适应缩放。
//
// 设计依据: .codeartsdoer/specs/gui_framework/spec.md 5.1.3
//           .codeartsdoer/specs/gui_framework/design.md 2.6
//
// 兼容策略: theme.h 宏委托至本接口; THEME_SCALE/THEME_SF 保留编译期常量。
#ifndef THEME_API_H
#define THEME_API_H

#include <stdint.h>

// ---- 令牌类别 (用于批量查询/调试) ----
typedef enum {
    TOKEN_CATEGORY_COLOR,
    TOKEN_CATEGORY_FONT,
    TOKEN_CATEGORY_FONT_SIZE,
    TOKEN_CATEGORY_RADIUS,
    TOKEN_CATEGORY_BORDER,
    TOKEN_CATEGORY_SHADOW,
    TOKEN_CATEGORY_PADDING,
    TOKEN_CATEGORY_MARGIN,
    TOKEN_CATEGORY_ANIM_DURATION,
    TOKEN_CATEGORY_ICON_SIZE,
} token_category_t;

// ---- 主题模式 ----
typedef enum {
    THEME_MODE_DARK = 0,
    THEME_MODE_LIGHT = 1,
} theme_mode_t;

// ---- 颜色令牌 ID (21 项, 覆盖背景/前景/强调/边框/阴影/任务栏/菜单/状态色) ----
// 顺序固定, 索引用于 theme.c 内部令牌表数组下标。
typedef enum {
    COLOR_BG = 0,            // 桌面/窗口背景
    COLOR_BG_PANEL,          // 面板/对话框背景
    COLOR_BG_TITLE,          // 标题栏背景
    COLOR_BG_TASKBAR,        // 任务栏背景
    COLOR_BG_MENU,           // 菜单/弹出背景
    COLOR_FG,                // 主要前景文字
    COLOR_FG_SOFT,           // 次要/柔和文字
    COLOR_FG_TITLE,          // 标题栏文字
    COLOR_ACCENT,            // 强调色 (按钮/选中/聚焦边框)
    COLOR_ACCENT_SOFT,       // 柔和强调 (悬停/浅蓝底)
    COLOR_BORDER,            // 普通边框/分隔线
    COLOR_BORDER_FOCUS,      // 聚焦边框
    COLOR_SHADOW,            // 阴影/描边
    COLOR_HOVER,             // 悬停态背景
    COLOR_PRESSED,           // 按下态背景
    COLOR_DISABLED,          // 禁用态背景
    COLOR_SUCCESS,           // 成功状态
    COLOR_WARNING,           // 警告状态
    COLOR_DANGER,            // 危险/错误状态
    COLOR_FIELD,             // 输入框背景
    COLOR_FIELD_FOCUS,       // 输入框聚焦背景
    COLOR_COUNT              // 令牌总数 = 21
} color_token_t;

// ---- 字体令牌 ID ----
typedef enum {
    FONT_TOKEN_CJK = 0,      // 中文字体
    FONT_TOKEN_ASCII,        // ASCII 字体
    FONT_TOKEN_TITLE,        // 标题字体
    FONT_TOKEN_BODY,         // 正文字体
    FONT_TOKEN_SMALL,        // 小号字体
    FONT_TOKEN_COUNT
} font_token_t;

// ---- 圆角令牌 ID ----
typedef enum {
    RADIUS_WINDOW_T = 0,     // 窗口圆角
    RADIUS_PANEL_T,          // 面板圆角
    RADIUS_CTRL_T,           // 控件圆角
    RADIUS_MENU_T,           // 菜单圆角
    RADIUS_TOKEN_COUNT
} radius_token_t;

// ---- 间距令牌 ID ----
typedef enum {
    PADDING_UNIT = 0,        // 基础单位
    PADDING_SM,              // 小档
    PADDING_MD,              // 中档
    PADDING_LG,              // 大档
    PADDING_CTRL,            // 控件内边距
    PADDING_PANEL,           // 面板内边距
    PADDING_COUNT
} padding_token_t;

// ---- 阴影令牌 ID ----
typedef enum {
    SHADOW_OFF_T = 0,        // 阴影偏移
    SHADOW_COL_T,            // 阴影颜色
    SHADOW_FOCUS_T,          // 聚焦档强度
    SHADOW_NORM_T,           // 普通档强度
    SHADOW_TOKEN_COUNT
} shadow_token_t;

// ---- 动画时长令牌 ID ----
typedef enum {
    ANIM_MENU_T = 0,         // 开始菜单展开/收起
    ANIM_WINDOW_T,           // 窗口最小化/最大化
    ANIM_FADE_T,             // 淡入淡出
    ANIM_TOKEN_COUNT
} anim_token_t;

// ==================== 查询接口 ====================

// 初始化主题系统。is_lfb: 1=LFB 32bpp 真彩, 0=8bpp 调色板。
// scale: gfx_font_scale() 返回值 (1=320x200, 2=640x480, 3=1280x720, 4=1920x1080)。
// 默认深色主题。
void theme_init(int is_lfb, int scale);

// 颜色查询: LFB 模式返回 0x00RRGGBB, 8bpp 模式返回调色板索引 (0..255)。
// 注意: 调用方需根据 gfx_is_lfb() 判断返回值语义。
uint32_t theme_get_color(color_token_t id);

// 颜色查询 (始终返回调色板索引): 用于 vga_put/gfx_fill_idx 等 8bpp 接口。
// LFB 模式下将真彩色量化至最接近的调色板索引。
uint8_t theme_get_color_idx(color_token_t id);

// 强调色快捷查询 (等价 theme_get_color(COLOR_ACCENT))。
uint32_t theme_get_accent(void);

// 字体规格查询
int theme_get_font_w(font_token_t id);
int theme_get_font_h(font_token_t id);
int theme_get_scale_factor(void);

// 圆角查询 (按 scale 缩放)
int theme_get_radius(radius_token_t id);

// 间距查询 (按 scale 缩放)
int theme_get_padding(padding_token_t id);

// 阴影查询: off=偏移px, col=调色板索引, intensity=强度档
int theme_get_shadow_off(void);
uint8_t theme_get_shadow_col(void);
int theme_get_shadow_intensity(int focused);

// 动画时长查询 (ms, 返回 0 表示动画关闭)
int theme_get_anim_duration(anim_token_t id);

// 毛玻璃模糊半径查询。返回 0 表示降级为半透明纯色 (8bpp 或性能不足)。
// LFB 真彩且 scale>=2 时返回 8, 否则返回 0。
int theme_get_blur_radius(void);

// 主题模式切换/查询
void theme_set_mode(theme_mode_t mode);
theme_mode_t theme_get_mode(void);

#endif // THEME_API_H