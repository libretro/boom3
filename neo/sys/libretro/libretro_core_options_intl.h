#ifndef LIBRETRO_CORE_OPTIONS_INTL_H__
#define LIBRETRO_CORE_OPTIONS_INTL_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1500 && _MSC_VER < 1900)
/* https://support.microsoft.com/en-us/kb/980263 */
#pragma execution_character_set("utf-8")
#pragma warning(disable:4566)
#endif

#include <libretro.h>

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/
/* RETRO_LANGUAGE_AR */

#define DOOM_FRAMERATE_LABEL_AR NULL
#define DOOM_FRAMERATE_INFO_0_AR NULL
#define OPTION_VAL_AUTO_AR "تلقائي"
#define OPTION_VAL_50_AR NULL
#define OPTION_VAL_60_AR NULL
#define OPTION_VAL_72_AR NULL
#define OPTION_VAL_75_AR NULL
#define OPTION_VAL_90_AR NULL
#define OPTION_VAL_100_AR NULL
#define OPTION_VAL_119_AR NULL
#define OPTION_VAL_120_AR NULL
#define OPTION_VAL_144_AR NULL
#define OPTION_VAL_155_AR NULL
#define OPTION_VAL_160_AR NULL
#define OPTION_VAL_165_AR NULL
#define OPTION_VAL_180_AR NULL
#define OPTION_VAL_200_AR NULL
#define OPTION_VAL_240_AR NULL
#define OPTION_VAL_244_AR NULL
#define OPTION_VAL_300_AR NULL
#define OPTION_VAL_360_AR NULL
#define DOOM_RESOLUTION_LABEL_AR "الدقة الداخلية (إعادة التشغيل مطلوبة)"
#define DOOM_RESOLUTION_INFO_0_AR NULL
#define OPTION_VAL_480X272_AR NULL
#define OPTION_VAL_640X368_AR NULL
#define OPTION_VAL_720X408_AR NULL
#define OPTION_VAL_960X544_AR NULL
#define OPTION_VAL_1280X720_AR NULL
#define OPTION_VAL_1920X1080_AR NULL
#define OPTION_VAL_2560X1440_AR NULL
#define OPTION_VAL_3840X2160_AR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_AR NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_AR NULL
#define DOOM_FPS_LABEL_AR NULL
#define DOOM_FPS_INFO_0_AR NULL

struct retro_core_option_v2_category option_cats_ar[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ar[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_AR,
      NULL,
      DOOM_FRAMERATE_INFO_0_AR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_AR},
         { "50",              OPTION_VAL_50_AR},
         { "60",              OPTION_VAL_60_AR},
         { "72",              OPTION_VAL_72_AR},
         { "75",              OPTION_VAL_75_AR},
         { "90",              OPTION_VAL_90_AR},
         { "100",              OPTION_VAL_100_AR},
         { "119",              OPTION_VAL_119_AR},
         { "120",              OPTION_VAL_120_AR},
         { "144",              OPTION_VAL_144_AR},
         { "155",              OPTION_VAL_155_AR},
         { "160",              OPTION_VAL_160_AR},
         { "165",              OPTION_VAL_165_AR},
         { "180",              OPTION_VAL_180_AR},
         { "200",              OPTION_VAL_200_AR},
         { "240",              OPTION_VAL_240_AR},
         { "244",              OPTION_VAL_244_AR},
         { "300",              OPTION_VAL_300_AR},
         { "360",              OPTION_VAL_360_AR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_AR,
      NULL,
      DOOM_RESOLUTION_INFO_0_AR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_AR },
         { "640x368",   OPTION_VAL_640X368_AR },
         { "720x408",   OPTION_VAL_720X408_AR },
         { "960x544",   OPTION_VAL_960X544_AR },
		 { "1280x720",   OPTION_VAL_1280X720_AR },
		 { "1920x1080",   OPTION_VAL_1920X1080_AR },
		 { "2560x1440",   OPTION_VAL_2560X1440_AR },
		 { "3840x2160",   OPTION_VAL_3840X2160_AR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_AR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_AR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_AR,
      NULL,
      DOOM_FPS_INFO_0_AR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ar = {
   option_cats_ar,
   option_defs_ar
};

/* RETRO_LANGUAGE_AST */

#define DOOM_FRAMERATE_LABEL_AST NULL
#define DOOM_FRAMERATE_INFO_0_AST NULL
#define OPTION_VAL_AUTO_AST NULL
#define OPTION_VAL_50_AST NULL
#define OPTION_VAL_60_AST NULL
#define OPTION_VAL_72_AST NULL
#define OPTION_VAL_75_AST NULL
#define OPTION_VAL_90_AST NULL
#define OPTION_VAL_100_AST NULL
#define OPTION_VAL_119_AST NULL
#define OPTION_VAL_120_AST NULL
#define OPTION_VAL_144_AST NULL
#define OPTION_VAL_155_AST NULL
#define OPTION_VAL_160_AST NULL
#define OPTION_VAL_165_AST NULL
#define OPTION_VAL_180_AST NULL
#define OPTION_VAL_200_AST NULL
#define OPTION_VAL_240_AST NULL
#define OPTION_VAL_244_AST NULL
#define OPTION_VAL_300_AST NULL
#define OPTION_VAL_360_AST NULL
#define DOOM_RESOLUTION_LABEL_AST NULL
#define DOOM_RESOLUTION_INFO_0_AST NULL
#define OPTION_VAL_480X272_AST NULL
#define OPTION_VAL_640X368_AST NULL
#define OPTION_VAL_720X408_AST NULL
#define OPTION_VAL_960X544_AST "960x544 (por defeutu)"
#define OPTION_VAL_1280X720_AST NULL
#define OPTION_VAL_1920X1080_AST NULL
#define OPTION_VAL_2560X1440_AST NULL
#define OPTION_VAL_3840X2160_AST NULL
#define DOOM_INVERT_Y_AXIS_LABEL_AST "Invertir la exa Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_AST "Invierte la exa Y de la palanca analóxica derecha."
#define DOOM_FPS_LABEL_AST "Amosar los FPS"
#define DOOM_FPS_INFO_0_AST NULL

struct retro_core_option_v2_category option_cats_ast[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ast[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_AST,
      NULL,
      DOOM_FRAMERATE_INFO_0_AST,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_AST},
         { "50",              OPTION_VAL_50_AST},
         { "60",              OPTION_VAL_60_AST},
         { "72",              OPTION_VAL_72_AST},
         { "75",              OPTION_VAL_75_AST},
         { "90",              OPTION_VAL_90_AST},
         { "100",              OPTION_VAL_100_AST},
         { "119",              OPTION_VAL_119_AST},
         { "120",              OPTION_VAL_120_AST},
         { "144",              OPTION_VAL_144_AST},
         { "155",              OPTION_VAL_155_AST},
         { "160",              OPTION_VAL_160_AST},
         { "165",              OPTION_VAL_165_AST},
         { "180",              OPTION_VAL_180_AST},
         { "200",              OPTION_VAL_200_AST},
         { "240",              OPTION_VAL_240_AST},
         { "244",              OPTION_VAL_244_AST},
         { "300",              OPTION_VAL_300_AST},
         { "360",              OPTION_VAL_360_AST},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_AST,
      NULL,
      DOOM_RESOLUTION_INFO_0_AST,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_AST },
         { "640x368",   OPTION_VAL_640X368_AST },
         { "720x408",   OPTION_VAL_720X408_AST },
         { "960x544",   OPTION_VAL_960X544_AST },
		 { "1280x720",   OPTION_VAL_1280X720_AST },
		 { "1920x1080",   OPTION_VAL_1920X1080_AST },
		 { "2560x1440",   OPTION_VAL_2560X1440_AST },
		 { "3840x2160",   OPTION_VAL_3840X2160_AST },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_AST,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_AST,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_AST,
      NULL,
      DOOM_FPS_INFO_0_AST,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ast = {
   option_cats_ast,
   option_defs_ast
};

/* RETRO_LANGUAGE_CA */

#define DOOM_FRAMERATE_LABEL_CA NULL
#define DOOM_FRAMERATE_INFO_0_CA NULL
#define OPTION_VAL_AUTO_CA "Automàtic"
#define OPTION_VAL_50_CA NULL
#define OPTION_VAL_60_CA NULL
#define OPTION_VAL_72_CA NULL
#define OPTION_VAL_75_CA NULL
#define OPTION_VAL_90_CA NULL
#define OPTION_VAL_100_CA NULL
#define OPTION_VAL_119_CA NULL
#define OPTION_VAL_120_CA NULL
#define OPTION_VAL_144_CA NULL
#define OPTION_VAL_155_CA NULL
#define OPTION_VAL_160_CA NULL
#define OPTION_VAL_165_CA NULL
#define OPTION_VAL_180_CA NULL
#define OPTION_VAL_200_CA NULL
#define OPTION_VAL_240_CA NULL
#define OPTION_VAL_244_CA NULL
#define OPTION_VAL_300_CA NULL
#define OPTION_VAL_360_CA NULL
#define DOOM_RESOLUTION_LABEL_CA NULL
#define DOOM_RESOLUTION_INFO_0_CA NULL
#define OPTION_VAL_480X272_CA "480×272"
#define OPTION_VAL_640X368_CA "640×368"
#define OPTION_VAL_720X408_CA "720×408"
#define OPTION_VAL_960X544_CA NULL
#define OPTION_VAL_1280X720_CA "1280×720"
#define OPTION_VAL_1920X1080_CA "1920×1080"
#define OPTION_VAL_2560X1440_CA "2560×1440"
#define OPTION_VAL_3840X2160_CA "3840×2160"
#define DOOM_INVERT_Y_AXIS_LABEL_CA NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_CA NULL
#define DOOM_FPS_LABEL_CA NULL
#define DOOM_FPS_INFO_0_CA NULL

struct retro_core_option_v2_category option_cats_ca[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ca[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_CA,
      NULL,
      DOOM_FRAMERATE_INFO_0_CA,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_CA},
         { "50",              OPTION_VAL_50_CA},
         { "60",              OPTION_VAL_60_CA},
         { "72",              OPTION_VAL_72_CA},
         { "75",              OPTION_VAL_75_CA},
         { "90",              OPTION_VAL_90_CA},
         { "100",              OPTION_VAL_100_CA},
         { "119",              OPTION_VAL_119_CA},
         { "120",              OPTION_VAL_120_CA},
         { "144",              OPTION_VAL_144_CA},
         { "155",              OPTION_VAL_155_CA},
         { "160",              OPTION_VAL_160_CA},
         { "165",              OPTION_VAL_165_CA},
         { "180",              OPTION_VAL_180_CA},
         { "200",              OPTION_VAL_200_CA},
         { "240",              OPTION_VAL_240_CA},
         { "244",              OPTION_VAL_244_CA},
         { "300",              OPTION_VAL_300_CA},
         { "360",              OPTION_VAL_360_CA},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_CA,
      NULL,
      DOOM_RESOLUTION_INFO_0_CA,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_CA },
         { "640x368",   OPTION_VAL_640X368_CA },
         { "720x408",   OPTION_VAL_720X408_CA },
         { "960x544",   OPTION_VAL_960X544_CA },
		 { "1280x720",   OPTION_VAL_1280X720_CA },
		 { "1920x1080",   OPTION_VAL_1920X1080_CA },
		 { "2560x1440",   OPTION_VAL_2560X1440_CA },
		 { "3840x2160",   OPTION_VAL_3840X2160_CA },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_CA,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_CA,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_CA,
      NULL,
      DOOM_FPS_INFO_0_CA,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ca = {
   option_cats_ca,
   option_defs_ca
};

/* RETRO_LANGUAGE_CHS */

#define DOOM_FRAMERATE_LABEL_CHS NULL
#define DOOM_FRAMERATE_INFO_0_CHS NULL
#define OPTION_VAL_AUTO_CHS "自动"
#define OPTION_VAL_50_CHS NULL
#define OPTION_VAL_60_CHS NULL
#define OPTION_VAL_72_CHS NULL
#define OPTION_VAL_75_CHS NULL
#define OPTION_VAL_90_CHS NULL
#define OPTION_VAL_100_CHS NULL
#define OPTION_VAL_119_CHS NULL
#define OPTION_VAL_120_CHS NULL
#define OPTION_VAL_144_CHS NULL
#define OPTION_VAL_155_CHS NULL
#define OPTION_VAL_160_CHS NULL
#define OPTION_VAL_165_CHS NULL
#define OPTION_VAL_180_CHS NULL
#define OPTION_VAL_200_CHS NULL
#define OPTION_VAL_240_CHS NULL
#define OPTION_VAL_244_CHS NULL
#define OPTION_VAL_300_CHS NULL
#define OPTION_VAL_360_CHS NULL
#define DOOM_RESOLUTION_LABEL_CHS "内部分辨率(需要重启)"
#define DOOM_RESOLUTION_INFO_0_CHS NULL
#define OPTION_VAL_480X272_CHS NULL
#define OPTION_VAL_640X368_CHS NULL
#define OPTION_VAL_720X408_CHS NULL
#define OPTION_VAL_960X544_CHS NULL
#define OPTION_VAL_1280X720_CHS NULL
#define OPTION_VAL_1920X1080_CHS NULL
#define OPTION_VAL_2560X1440_CHS NULL
#define OPTION_VAL_3840X2160_CHS NULL
#define DOOM_INVERT_Y_AXIS_LABEL_CHS NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_CHS NULL
#define DOOM_FPS_LABEL_CHS "显示 FPS"
#define DOOM_FPS_INFO_0_CHS NULL

struct retro_core_option_v2_category option_cats_chs[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_chs[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_CHS,
      NULL,
      DOOM_FRAMERATE_INFO_0_CHS,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_CHS},
         { "50",              OPTION_VAL_50_CHS},
         { "60",              OPTION_VAL_60_CHS},
         { "72",              OPTION_VAL_72_CHS},
         { "75",              OPTION_VAL_75_CHS},
         { "90",              OPTION_VAL_90_CHS},
         { "100",              OPTION_VAL_100_CHS},
         { "119",              OPTION_VAL_119_CHS},
         { "120",              OPTION_VAL_120_CHS},
         { "144",              OPTION_VAL_144_CHS},
         { "155",              OPTION_VAL_155_CHS},
         { "160",              OPTION_VAL_160_CHS},
         { "165",              OPTION_VAL_165_CHS},
         { "180",              OPTION_VAL_180_CHS},
         { "200",              OPTION_VAL_200_CHS},
         { "240",              OPTION_VAL_240_CHS},
         { "244",              OPTION_VAL_244_CHS},
         { "300",              OPTION_VAL_300_CHS},
         { "360",              OPTION_VAL_360_CHS},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_CHS,
      NULL,
      DOOM_RESOLUTION_INFO_0_CHS,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_CHS },
         { "640x368",   OPTION_VAL_640X368_CHS },
         { "720x408",   OPTION_VAL_720X408_CHS },
         { "960x544",   OPTION_VAL_960X544_CHS },
		 { "1280x720",   OPTION_VAL_1280X720_CHS },
		 { "1920x1080",   OPTION_VAL_1920X1080_CHS },
		 { "2560x1440",   OPTION_VAL_2560X1440_CHS },
		 { "3840x2160",   OPTION_VAL_3840X2160_CHS },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_CHS,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_CHS,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_CHS,
      NULL,
      DOOM_FPS_INFO_0_CHS,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_chs = {
   option_cats_chs,
   option_defs_chs
};

/* RETRO_LANGUAGE_CHT */

#define DOOM_FRAMERATE_LABEL_CHT "影格率 (需要重新啟動)"
#define DOOM_FRAMERATE_INFO_0_CHT "設定影格速率。"
#define OPTION_VAL_AUTO_CHT "自動"
#define OPTION_VAL_50_CHT NULL
#define OPTION_VAL_60_CHT NULL
#define OPTION_VAL_72_CHT NULL
#define OPTION_VAL_75_CHT NULL
#define OPTION_VAL_90_CHT NULL
#define OPTION_VAL_100_CHT NULL
#define OPTION_VAL_119_CHT NULL
#define OPTION_VAL_120_CHT NULL
#define OPTION_VAL_144_CHT NULL
#define OPTION_VAL_155_CHT NULL
#define OPTION_VAL_160_CHT NULL
#define OPTION_VAL_165_CHT NULL
#define OPTION_VAL_180_CHT NULL
#define OPTION_VAL_200_CHT NULL
#define OPTION_VAL_240_CHT NULL
#define OPTION_VAL_244_CHT NULL
#define OPTION_VAL_300_CHT NULL
#define OPTION_VAL_360_CHT NULL
#define DOOM_RESOLUTION_LABEL_CHT "內部解析度 (需要重新啟動)"
#define DOOM_RESOLUTION_INFO_0_CHT "設定解析度比例。"
#define OPTION_VAL_480X272_CHT NULL
#define OPTION_VAL_640X368_CHT NULL
#define OPTION_VAL_720X408_CHT NULL
#define OPTION_VAL_960X544_CHT "960x544 (預設)"
#define OPTION_VAL_1280X720_CHT NULL
#define OPTION_VAL_1920X1080_CHT NULL
#define OPTION_VAL_2560X1440_CHT NULL
#define OPTION_VAL_3840X2160_CHT NULL
#define DOOM_INVERT_Y_AXIS_LABEL_CHT "垂直反轉"
#define DOOM_INVERT_Y_AXIS_INFO_0_CHT "變更右類比搖桿垂直方向。"
#define DOOM_FPS_LABEL_CHT "顯示FPS"
#define DOOM_FPS_INFO_0_CHT "在螢幕上顯示幀率"

struct retro_core_option_v2_category option_cats_cht[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_cht[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_CHT,
      NULL,
      DOOM_FRAMERATE_INFO_0_CHT,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_CHT},
         { "50",              OPTION_VAL_50_CHT},
         { "60",              OPTION_VAL_60_CHT},
         { "72",              OPTION_VAL_72_CHT},
         { "75",              OPTION_VAL_75_CHT},
         { "90",              OPTION_VAL_90_CHT},
         { "100",              OPTION_VAL_100_CHT},
         { "119",              OPTION_VAL_119_CHT},
         { "120",              OPTION_VAL_120_CHT},
         { "144",              OPTION_VAL_144_CHT},
         { "155",              OPTION_VAL_155_CHT},
         { "160",              OPTION_VAL_160_CHT},
         { "165",              OPTION_VAL_165_CHT},
         { "180",              OPTION_VAL_180_CHT},
         { "200",              OPTION_VAL_200_CHT},
         { "240",              OPTION_VAL_240_CHT},
         { "244",              OPTION_VAL_244_CHT},
         { "300",              OPTION_VAL_300_CHT},
         { "360",              OPTION_VAL_360_CHT},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_CHT,
      NULL,
      DOOM_RESOLUTION_INFO_0_CHT,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_CHT },
         { "640x368",   OPTION_VAL_640X368_CHT },
         { "720x408",   OPTION_VAL_720X408_CHT },
         { "960x544",   OPTION_VAL_960X544_CHT },
		 { "1280x720",   OPTION_VAL_1280X720_CHT },
		 { "1920x1080",   OPTION_VAL_1920X1080_CHT },
		 { "2560x1440",   OPTION_VAL_2560X1440_CHT },
		 { "3840x2160",   OPTION_VAL_3840X2160_CHT },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_CHT,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_CHT,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_CHT,
      NULL,
      DOOM_FPS_INFO_0_CHT,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_cht = {
   option_cats_cht,
   option_defs_cht
};

/* RETRO_LANGUAGE_CS */

#define DOOM_FRAMERATE_LABEL_CS "Rychlost Snímků (Vyžadovaný Restart)"
#define DOOM_FRAMERATE_INFO_0_CS "Zvolte požadovanou snímkovou frekvenci."
#define OPTION_VAL_AUTO_CS "Automatická"
#define OPTION_VAL_50_CS NULL
#define OPTION_VAL_60_CS NULL
#define OPTION_VAL_72_CS NULL
#define OPTION_VAL_75_CS NULL
#define OPTION_VAL_90_CS NULL
#define OPTION_VAL_100_CS NULL
#define OPTION_VAL_119_CS NULL
#define OPTION_VAL_120_CS NULL
#define OPTION_VAL_144_CS NULL
#define OPTION_VAL_155_CS NULL
#define OPTION_VAL_160_CS NULL
#define OPTION_VAL_165_CS NULL
#define OPTION_VAL_180_CS NULL
#define OPTION_VAL_200_CS NULL
#define OPTION_VAL_240_CS NULL
#define OPTION_VAL_244_CS NULL
#define OPTION_VAL_300_CS NULL
#define OPTION_VAL_360_CS NULL
#define DOOM_RESOLUTION_LABEL_CS "Internal resolution (Vyžaduje restart)"
#define DOOM_RESOLUTION_INFO_0_CS "Vyberte rozlišení pro vykreslování."
#define OPTION_VAL_480X272_CS NULL
#define OPTION_VAL_640X368_CS NULL
#define OPTION_VAL_720X408_CS NULL
#define OPTION_VAL_960X544_CS "960x544 (Výchozí)"
#define OPTION_VAL_1280X720_CS NULL
#define OPTION_VAL_1920X1080_CS NULL
#define OPTION_VAL_2560X1440_CS NULL
#define OPTION_VAL_3840X2160_CS NULL
#define DOOM_INVERT_Y_AXIS_LABEL_CS "Invertovat Osu Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_CS "Invertujte osu Y pravé analogové páčky."
#define DOOM_FPS_LABEL_CS "Ukázat FPS"
#define DOOM_FPS_INFO_0_CS "Zobrazení snímkové frekvence na obrazovce."

struct retro_core_option_v2_category option_cats_cs[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_cs[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_CS,
      NULL,
      DOOM_FRAMERATE_INFO_0_CS,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_CS},
         { "50",              OPTION_VAL_50_CS},
         { "60",              OPTION_VAL_60_CS},
         { "72",              OPTION_VAL_72_CS},
         { "75",              OPTION_VAL_75_CS},
         { "90",              OPTION_VAL_90_CS},
         { "100",              OPTION_VAL_100_CS},
         { "119",              OPTION_VAL_119_CS},
         { "120",              OPTION_VAL_120_CS},
         { "144",              OPTION_VAL_144_CS},
         { "155",              OPTION_VAL_155_CS},
         { "160",              OPTION_VAL_160_CS},
         { "165",              OPTION_VAL_165_CS},
         { "180",              OPTION_VAL_180_CS},
         { "200",              OPTION_VAL_200_CS},
         { "240",              OPTION_VAL_240_CS},
         { "244",              OPTION_VAL_244_CS},
         { "300",              OPTION_VAL_300_CS},
         { "360",              OPTION_VAL_360_CS},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_CS,
      NULL,
      DOOM_RESOLUTION_INFO_0_CS,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_CS },
         { "640x368",   OPTION_VAL_640X368_CS },
         { "720x408",   OPTION_VAL_720X408_CS },
         { "960x544",   OPTION_VAL_960X544_CS },
		 { "1280x720",   OPTION_VAL_1280X720_CS },
		 { "1920x1080",   OPTION_VAL_1920X1080_CS },
		 { "2560x1440",   OPTION_VAL_2560X1440_CS },
		 { "3840x2160",   OPTION_VAL_3840X2160_CS },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_CS,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_CS,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_CS,
      NULL,
      DOOM_FPS_INFO_0_CS,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_cs = {
   option_cats_cs,
   option_defs_cs
};

/* RETRO_LANGUAGE_CY */

#define DOOM_FRAMERATE_LABEL_CY NULL
#define DOOM_FRAMERATE_INFO_0_CY NULL
#define OPTION_VAL_AUTO_CY NULL
#define OPTION_VAL_50_CY NULL
#define OPTION_VAL_60_CY NULL
#define OPTION_VAL_72_CY NULL
#define OPTION_VAL_75_CY NULL
#define OPTION_VAL_90_CY NULL
#define OPTION_VAL_100_CY NULL
#define OPTION_VAL_119_CY NULL
#define OPTION_VAL_120_CY NULL
#define OPTION_VAL_144_CY NULL
#define OPTION_VAL_155_CY NULL
#define OPTION_VAL_160_CY NULL
#define OPTION_VAL_165_CY NULL
#define OPTION_VAL_180_CY NULL
#define OPTION_VAL_200_CY NULL
#define OPTION_VAL_240_CY NULL
#define OPTION_VAL_244_CY NULL
#define OPTION_VAL_300_CY NULL
#define OPTION_VAL_360_CY NULL
#define DOOM_RESOLUTION_LABEL_CY NULL
#define DOOM_RESOLUTION_INFO_0_CY NULL
#define OPTION_VAL_480X272_CY NULL
#define OPTION_VAL_640X368_CY NULL
#define OPTION_VAL_720X408_CY NULL
#define OPTION_VAL_960X544_CY NULL
#define OPTION_VAL_1280X720_CY NULL
#define OPTION_VAL_1920X1080_CY NULL
#define OPTION_VAL_2560X1440_CY NULL
#define OPTION_VAL_3840X2160_CY NULL
#define DOOM_INVERT_Y_AXIS_LABEL_CY NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_CY NULL
#define DOOM_FPS_LABEL_CY NULL
#define DOOM_FPS_INFO_0_CY NULL

struct retro_core_option_v2_category option_cats_cy[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_cy[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_CY,
      NULL,
      DOOM_FRAMERATE_INFO_0_CY,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_CY},
         { "50",              OPTION_VAL_50_CY},
         { "60",              OPTION_VAL_60_CY},
         { "72",              OPTION_VAL_72_CY},
         { "75",              OPTION_VAL_75_CY},
         { "90",              OPTION_VAL_90_CY},
         { "100",              OPTION_VAL_100_CY},
         { "119",              OPTION_VAL_119_CY},
         { "120",              OPTION_VAL_120_CY},
         { "144",              OPTION_VAL_144_CY},
         { "155",              OPTION_VAL_155_CY},
         { "160",              OPTION_VAL_160_CY},
         { "165",              OPTION_VAL_165_CY},
         { "180",              OPTION_VAL_180_CY},
         { "200",              OPTION_VAL_200_CY},
         { "240",              OPTION_VAL_240_CY},
         { "244",              OPTION_VAL_244_CY},
         { "300",              OPTION_VAL_300_CY},
         { "360",              OPTION_VAL_360_CY},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_CY,
      NULL,
      DOOM_RESOLUTION_INFO_0_CY,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_CY },
         { "640x368",   OPTION_VAL_640X368_CY },
         { "720x408",   OPTION_VAL_720X408_CY },
         { "960x544",   OPTION_VAL_960X544_CY },
		 { "1280x720",   OPTION_VAL_1280X720_CY },
		 { "1920x1080",   OPTION_VAL_1920X1080_CY },
		 { "2560x1440",   OPTION_VAL_2560X1440_CY },
		 { "3840x2160",   OPTION_VAL_3840X2160_CY },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_CY,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_CY,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_CY,
      NULL,
      DOOM_FPS_INFO_0_CY,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_cy = {
   option_cats_cy,
   option_defs_cy
};

/* RETRO_LANGUAGE_DA */

#define DOOM_FRAMERATE_LABEL_DA NULL
#define DOOM_FRAMERATE_INFO_0_DA NULL
#define OPTION_VAL_AUTO_DA NULL
#define OPTION_VAL_50_DA NULL
#define OPTION_VAL_60_DA NULL
#define OPTION_VAL_72_DA NULL
#define OPTION_VAL_75_DA NULL
#define OPTION_VAL_90_DA NULL
#define OPTION_VAL_100_DA NULL
#define OPTION_VAL_119_DA NULL
#define OPTION_VAL_120_DA NULL
#define OPTION_VAL_144_DA NULL
#define OPTION_VAL_155_DA NULL
#define OPTION_VAL_160_DA NULL
#define OPTION_VAL_165_DA NULL
#define OPTION_VAL_180_DA NULL
#define OPTION_VAL_200_DA NULL
#define OPTION_VAL_240_DA NULL
#define OPTION_VAL_244_DA NULL
#define OPTION_VAL_300_DA NULL
#define OPTION_VAL_360_DA NULL
#define DOOM_RESOLUTION_LABEL_DA NULL
#define DOOM_RESOLUTION_INFO_0_DA NULL
#define OPTION_VAL_480X272_DA NULL
#define OPTION_VAL_640X368_DA NULL
#define OPTION_VAL_720X408_DA NULL
#define OPTION_VAL_960X544_DA NULL
#define OPTION_VAL_1280X720_DA NULL
#define OPTION_VAL_1920X1080_DA NULL
#define OPTION_VAL_2560X1440_DA NULL
#define OPTION_VAL_3840X2160_DA NULL
#define DOOM_INVERT_Y_AXIS_LABEL_DA NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_DA NULL
#define DOOM_FPS_LABEL_DA NULL
#define DOOM_FPS_INFO_0_DA NULL

struct retro_core_option_v2_category option_cats_da[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_da[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_DA,
      NULL,
      DOOM_FRAMERATE_INFO_0_DA,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_DA},
         { "50",              OPTION_VAL_50_DA},
         { "60",              OPTION_VAL_60_DA},
         { "72",              OPTION_VAL_72_DA},
         { "75",              OPTION_VAL_75_DA},
         { "90",              OPTION_VAL_90_DA},
         { "100",              OPTION_VAL_100_DA},
         { "119",              OPTION_VAL_119_DA},
         { "120",              OPTION_VAL_120_DA},
         { "144",              OPTION_VAL_144_DA},
         { "155",              OPTION_VAL_155_DA},
         { "160",              OPTION_VAL_160_DA},
         { "165",              OPTION_VAL_165_DA},
         { "180",              OPTION_VAL_180_DA},
         { "200",              OPTION_VAL_200_DA},
         { "240",              OPTION_VAL_240_DA},
         { "244",              OPTION_VAL_244_DA},
         { "300",              OPTION_VAL_300_DA},
         { "360",              OPTION_VAL_360_DA},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_DA,
      NULL,
      DOOM_RESOLUTION_INFO_0_DA,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_DA },
         { "640x368",   OPTION_VAL_640X368_DA },
         { "720x408",   OPTION_VAL_720X408_DA },
         { "960x544",   OPTION_VAL_960X544_DA },
		 { "1280x720",   OPTION_VAL_1280X720_DA },
		 { "1920x1080",   OPTION_VAL_1920X1080_DA },
		 { "2560x1440",   OPTION_VAL_2560X1440_DA },
		 { "3840x2160",   OPTION_VAL_3840X2160_DA },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_DA,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_DA,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_DA,
      NULL,
      DOOM_FPS_INFO_0_DA,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_da = {
   option_cats_da,
   option_defs_da
};

/* RETRO_LANGUAGE_DE */

#define DOOM_FRAMERATE_LABEL_DE "Bildfrequenz (Neustart erforderlich)"
#define DOOM_FRAMERATE_INFO_0_DE "Die gewünschte Bildrate wählen."
#define OPTION_VAL_AUTO_DE "Automatisch"
#define OPTION_VAL_50_DE "50 fps"
#define OPTION_VAL_60_DE "60 fps"
#define OPTION_VAL_72_DE "72 fps"
#define OPTION_VAL_75_DE "75 fps"
#define OPTION_VAL_90_DE "90 fps"
#define OPTION_VAL_100_DE "100 fps"
#define OPTION_VAL_119_DE "119 fps"
#define OPTION_VAL_120_DE "120 fps"
#define OPTION_VAL_144_DE "144 fps"
#define OPTION_VAL_155_DE "155 fps"
#define OPTION_VAL_160_DE "160 fps"
#define OPTION_VAL_165_DE "165 fps"
#define OPTION_VAL_180_DE "180 fps"
#define OPTION_VAL_200_DE "200 fps"
#define OPTION_VAL_240_DE "240 fps"
#define OPTION_VAL_244_DE "244 fps"
#define OPTION_VAL_300_DE "300 fps"
#define OPTION_VAL_360_DE "360 fps"
#define DOOM_RESOLUTION_LABEL_DE "Interne Auflösung (Neustart erforderlich)"
#define DOOM_RESOLUTION_INFO_0_DE "Auflösung wählen, mit der gerendert werden soll."
#define OPTION_VAL_480X272_DE "480 x 272"
#define OPTION_VAL_640X368_DE "640 x 368"
#define OPTION_VAL_720X408_DE "720 x 408"
#define OPTION_VAL_960X544_DE "960 × 544 (Standard)"
#define OPTION_VAL_1280X720_DE "1280 x 720"
#define OPTION_VAL_1920X1080_DE "1920 x 1080"
#define OPTION_VAL_2560X1440_DE "2560 x 1440"
#define OPTION_VAL_3840X2160_DE "3840 x 2160"
#define DOOM_INVERT_Y_AXIS_LABEL_DE "Y-Achse invertieren"
#define DOOM_INVERT_Y_AXIS_INFO_0_DE "Die Y-Achse des rechten analogen Sticks invertieren."
#define DOOM_FPS_LABEL_DE "FPS anzeigen"
#define DOOM_FPS_INFO_0_DE "Bildrate auf dem Bildschirm anzeigen."

struct retro_core_option_v2_category option_cats_de[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_de[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_DE,
      NULL,
      DOOM_FRAMERATE_INFO_0_DE,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_DE},
         { "50",              OPTION_VAL_50_DE},
         { "60",              OPTION_VAL_60_DE},
         { "72",              OPTION_VAL_72_DE},
         { "75",              OPTION_VAL_75_DE},
         { "90",              OPTION_VAL_90_DE},
         { "100",              OPTION_VAL_100_DE},
         { "119",              OPTION_VAL_119_DE},
         { "120",              OPTION_VAL_120_DE},
         { "144",              OPTION_VAL_144_DE},
         { "155",              OPTION_VAL_155_DE},
         { "160",              OPTION_VAL_160_DE},
         { "165",              OPTION_VAL_165_DE},
         { "180",              OPTION_VAL_180_DE},
         { "200",              OPTION_VAL_200_DE},
         { "240",              OPTION_VAL_240_DE},
         { "244",              OPTION_VAL_244_DE},
         { "300",              OPTION_VAL_300_DE},
         { "360",              OPTION_VAL_360_DE},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_DE,
      NULL,
      DOOM_RESOLUTION_INFO_0_DE,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_DE },
         { "640x368",   OPTION_VAL_640X368_DE },
         { "720x408",   OPTION_VAL_720X408_DE },
         { "960x544",   OPTION_VAL_960X544_DE },
		 { "1280x720",   OPTION_VAL_1280X720_DE },
		 { "1920x1080",   OPTION_VAL_1920X1080_DE },
		 { "2560x1440",   OPTION_VAL_2560X1440_DE },
		 { "3840x2160",   OPTION_VAL_3840X2160_DE },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_DE,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_DE,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_DE,
      NULL,
      DOOM_FPS_INFO_0_DE,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_de = {
   option_cats_de,
   option_defs_de
};

/* RETRO_LANGUAGE_EL */

#define DOOM_FRAMERATE_LABEL_EL "Ρυθμός καρέ (Απαιτείται Επανεκκίνηση)"
#define DOOM_FRAMERATE_INFO_0_EL NULL
#define OPTION_VAL_AUTO_EL "Αυτόματο"
#define OPTION_VAL_50_EL "50 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_60_EL "60 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_72_EL "72 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_75_EL "75 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_90_EL "90 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_100_EL "100 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_119_EL "119 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_120_EL "120 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_144_EL "144 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_155_EL "155 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_160_EL "160 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_165_EL "165 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_180_EL "180 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_200_EL "200 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_240_EL "240 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_244_EL "244 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_300_EL "300 καρέ ανά δευτερόλεπτο"
#define OPTION_VAL_360_EL "360 καρέ ανά δευτερόλεπτο"
#define DOOM_RESOLUTION_LABEL_EL "Εσωτερική ανάλυση (Απαιτείται Επανεκκίνηση)"
#define DOOM_RESOLUTION_INFO_0_EL NULL
#define OPTION_VAL_480X272_EL NULL
#define OPTION_VAL_640X368_EL NULL
#define OPTION_VAL_720X408_EL NULL
#define OPTION_VAL_960X544_EL NULL
#define OPTION_VAL_1280X720_EL NULL
#define OPTION_VAL_1920X1080_EL NULL
#define OPTION_VAL_2560X1440_EL NULL
#define OPTION_VAL_3840X2160_EL NULL
#define DOOM_INVERT_Y_AXIS_LABEL_EL "Αντιστροφή Άξονα Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_EL "Αντιστροφή άξονα Y του δεξιού αναλογικού μοχλού."
#define DOOM_FPS_LABEL_EL "Εμφάνιση των καρέ ανά δευτερόλεπτο (FPS)"
#define DOOM_FPS_INFO_0_EL "Εμφάνιση ρυθμού καρέ στην οθόνη."

struct retro_core_option_v2_category option_cats_el[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_el[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_EL,
      NULL,
      DOOM_FRAMERATE_INFO_0_EL,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_EL},
         { "50",              OPTION_VAL_50_EL},
         { "60",              OPTION_VAL_60_EL},
         { "72",              OPTION_VAL_72_EL},
         { "75",              OPTION_VAL_75_EL},
         { "90",              OPTION_VAL_90_EL},
         { "100",              OPTION_VAL_100_EL},
         { "119",              OPTION_VAL_119_EL},
         { "120",              OPTION_VAL_120_EL},
         { "144",              OPTION_VAL_144_EL},
         { "155",              OPTION_VAL_155_EL},
         { "160",              OPTION_VAL_160_EL},
         { "165",              OPTION_VAL_165_EL},
         { "180",              OPTION_VAL_180_EL},
         { "200",              OPTION_VAL_200_EL},
         { "240",              OPTION_VAL_240_EL},
         { "244",              OPTION_VAL_244_EL},
         { "300",              OPTION_VAL_300_EL},
         { "360",              OPTION_VAL_360_EL},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_EL,
      NULL,
      DOOM_RESOLUTION_INFO_0_EL,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_EL },
         { "640x368",   OPTION_VAL_640X368_EL },
         { "720x408",   OPTION_VAL_720X408_EL },
         { "960x544",   OPTION_VAL_960X544_EL },
		 { "1280x720",   OPTION_VAL_1280X720_EL },
		 { "1920x1080",   OPTION_VAL_1920X1080_EL },
		 { "2560x1440",   OPTION_VAL_2560X1440_EL },
		 { "3840x2160",   OPTION_VAL_3840X2160_EL },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_EL,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_EL,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_EL,
      NULL,
      DOOM_FPS_INFO_0_EL,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_el = {
   option_cats_el,
   option_defs_el
};

/* RETRO_LANGUAGE_EN */

#define DOOM_FRAMERATE_LABEL_EN "Frame-rate (Restart Required)"
#define DOOM_FRAMERATE_INFO_0_EN "Choose the desired frame-rate."
#define OPTION_VAL_AUTO_EN NULL
#define OPTION_VAL_50_EN NULL
#define OPTION_VAL_60_EN NULL
#define OPTION_VAL_72_EN NULL
#define OPTION_VAL_75_EN NULL
#define OPTION_VAL_90_EN NULL
#define OPTION_VAL_100_EN NULL
#define OPTION_VAL_119_EN NULL
#define OPTION_VAL_120_EN NULL
#define OPTION_VAL_144_EN NULL
#define OPTION_VAL_155_EN NULL
#define OPTION_VAL_160_EN NULL
#define OPTION_VAL_165_EN NULL
#define OPTION_VAL_180_EN NULL
#define OPTION_VAL_200_EN NULL
#define OPTION_VAL_240_EN NULL
#define OPTION_VAL_244_EN NULL
#define OPTION_VAL_300_EN NULL
#define OPTION_VAL_360_EN NULL
#define DOOM_RESOLUTION_LABEL_EN NULL
#define DOOM_RESOLUTION_INFO_0_EN NULL
#define OPTION_VAL_480X272_EN NULL
#define OPTION_VAL_640X368_EN NULL
#define OPTION_VAL_720X408_EN NULL
#define OPTION_VAL_960X544_EN NULL
#define OPTION_VAL_1280X720_EN NULL
#define OPTION_VAL_1920X1080_EN NULL
#define OPTION_VAL_2560X1440_EN NULL
#define OPTION_VAL_3840X2160_EN NULL
#define DOOM_INVERT_Y_AXIS_LABEL_EN NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_EN "Invert the right analogue stick's Y axis."
#define DOOM_FPS_LABEL_EN NULL
#define DOOM_FPS_INFO_0_EN NULL

struct retro_core_option_v2_category option_cats_en[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_en[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_EN,
      NULL,
      DOOM_FRAMERATE_INFO_0_EN,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_EN},
         { "50",              OPTION_VAL_50_EN},
         { "60",              OPTION_VAL_60_EN},
         { "72",              OPTION_VAL_72_EN},
         { "75",              OPTION_VAL_75_EN},
         { "90",              OPTION_VAL_90_EN},
         { "100",              OPTION_VAL_100_EN},
         { "119",              OPTION_VAL_119_EN},
         { "120",              OPTION_VAL_120_EN},
         { "144",              OPTION_VAL_144_EN},
         { "155",              OPTION_VAL_155_EN},
         { "160",              OPTION_VAL_160_EN},
         { "165",              OPTION_VAL_165_EN},
         { "180",              OPTION_VAL_180_EN},
         { "200",              OPTION_VAL_200_EN},
         { "240",              OPTION_VAL_240_EN},
         { "244",              OPTION_VAL_244_EN},
         { "300",              OPTION_VAL_300_EN},
         { "360",              OPTION_VAL_360_EN},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_EN,
      NULL,
      DOOM_RESOLUTION_INFO_0_EN,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_EN },
         { "640x368",   OPTION_VAL_640X368_EN },
         { "720x408",   OPTION_VAL_720X408_EN },
         { "960x544",   OPTION_VAL_960X544_EN },
		 { "1280x720",   OPTION_VAL_1280X720_EN },
		 { "1920x1080",   OPTION_VAL_1920X1080_EN },
		 { "2560x1440",   OPTION_VAL_2560X1440_EN },
		 { "3840x2160",   OPTION_VAL_3840X2160_EN },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_EN,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_EN,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_EN,
      NULL,
      DOOM_FPS_INFO_0_EN,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_en = {
   option_cats_en,
   option_defs_en
};

/* RETRO_LANGUAGE_EO */

#define DOOM_FRAMERATE_LABEL_EO NULL
#define DOOM_FRAMERATE_INFO_0_EO NULL
#define OPTION_VAL_AUTO_EO NULL
#define OPTION_VAL_50_EO NULL
#define OPTION_VAL_60_EO NULL
#define OPTION_VAL_72_EO NULL
#define OPTION_VAL_75_EO NULL
#define OPTION_VAL_90_EO NULL
#define OPTION_VAL_100_EO NULL
#define OPTION_VAL_119_EO NULL
#define OPTION_VAL_120_EO NULL
#define OPTION_VAL_144_EO NULL
#define OPTION_VAL_155_EO NULL
#define OPTION_VAL_160_EO NULL
#define OPTION_VAL_165_EO NULL
#define OPTION_VAL_180_EO NULL
#define OPTION_VAL_200_EO NULL
#define OPTION_VAL_240_EO NULL
#define OPTION_VAL_244_EO NULL
#define OPTION_VAL_300_EO NULL
#define OPTION_VAL_360_EO NULL
#define DOOM_RESOLUTION_LABEL_EO NULL
#define DOOM_RESOLUTION_INFO_0_EO NULL
#define OPTION_VAL_480X272_EO NULL
#define OPTION_VAL_640X368_EO NULL
#define OPTION_VAL_720X408_EO NULL
#define OPTION_VAL_960X544_EO NULL
#define OPTION_VAL_1280X720_EO NULL
#define OPTION_VAL_1920X1080_EO NULL
#define OPTION_VAL_2560X1440_EO NULL
#define OPTION_VAL_3840X2160_EO NULL
#define DOOM_INVERT_Y_AXIS_LABEL_EO NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_EO NULL
#define DOOM_FPS_LABEL_EO NULL
#define DOOM_FPS_INFO_0_EO NULL

struct retro_core_option_v2_category option_cats_eo[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_eo[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_EO,
      NULL,
      DOOM_FRAMERATE_INFO_0_EO,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_EO},
         { "50",              OPTION_VAL_50_EO},
         { "60",              OPTION_VAL_60_EO},
         { "72",              OPTION_VAL_72_EO},
         { "75",              OPTION_VAL_75_EO},
         { "90",              OPTION_VAL_90_EO},
         { "100",              OPTION_VAL_100_EO},
         { "119",              OPTION_VAL_119_EO},
         { "120",              OPTION_VAL_120_EO},
         { "144",              OPTION_VAL_144_EO},
         { "155",              OPTION_VAL_155_EO},
         { "160",              OPTION_VAL_160_EO},
         { "165",              OPTION_VAL_165_EO},
         { "180",              OPTION_VAL_180_EO},
         { "200",              OPTION_VAL_200_EO},
         { "240",              OPTION_VAL_240_EO},
         { "244",              OPTION_VAL_244_EO},
         { "300",              OPTION_VAL_300_EO},
         { "360",              OPTION_VAL_360_EO},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_EO,
      NULL,
      DOOM_RESOLUTION_INFO_0_EO,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_EO },
         { "640x368",   OPTION_VAL_640X368_EO },
         { "720x408",   OPTION_VAL_720X408_EO },
         { "960x544",   OPTION_VAL_960X544_EO },
		 { "1280x720",   OPTION_VAL_1280X720_EO },
		 { "1920x1080",   OPTION_VAL_1920X1080_EO },
		 { "2560x1440",   OPTION_VAL_2560X1440_EO },
		 { "3840x2160",   OPTION_VAL_3840X2160_EO },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_EO,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_EO,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_EO,
      NULL,
      DOOM_FPS_INFO_0_EO,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_eo = {
   option_cats_eo,
   option_defs_eo
};

/* RETRO_LANGUAGE_ES */

#define DOOM_FRAMERATE_LABEL_ES "Velocidad de FPS (es necesario reiniciar)"
#define DOOM_FRAMERATE_INFO_0_ES "Selecciona la velocidad de fotogramas deseada."
#define OPTION_VAL_AUTO_ES "Selección automática"
#define OPTION_VAL_50_ES "50 FPS"
#define OPTION_VAL_60_ES "60 FPS"
#define OPTION_VAL_72_ES "72 FPS"
#define OPTION_VAL_75_ES "75 FPS"
#define OPTION_VAL_90_ES "90 FPS"
#define OPTION_VAL_100_ES "100 FPS"
#define OPTION_VAL_119_ES "119 FPS"
#define OPTION_VAL_120_ES "120 FPS"
#define OPTION_VAL_144_ES "144 FPS"
#define OPTION_VAL_155_ES "155 FPS"
#define OPTION_VAL_160_ES "160 FPS"
#define OPTION_VAL_165_ES "165 FPS"
#define OPTION_VAL_180_ES "180 FPS"
#define OPTION_VAL_200_ES "200 FPS"
#define OPTION_VAL_240_ES "240 FPS"
#define OPTION_VAL_244_ES "244 FPS"
#define OPTION_VAL_300_ES "300 FPS"
#define OPTION_VAL_360_ES "360 FPS"
#define DOOM_RESOLUTION_LABEL_ES "Resolución interna (es necesario reiniciar)"
#define DOOM_RESOLUTION_INFO_0_ES "Selecciona la resolución de renderizado."
#define OPTION_VAL_480X272_ES "480 × 272"
#define OPTION_VAL_640X368_ES "640 × 368"
#define OPTION_VAL_720X408_ES "720 × 408"
#define OPTION_VAL_960X544_ES "960x544 (por defecto)"
#define OPTION_VAL_1280X720_ES "1280 × 720"
#define OPTION_VAL_1920X1080_ES "1920 × 1080"
#define OPTION_VAL_2560X1440_ES "2560 × 1440"
#define OPTION_VAL_3840X2160_ES "3840 × 2160"
#define DOOM_INVERT_Y_AXIS_LABEL_ES "Invertir el eje Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_ES "Invierte el eje Y del stick analógico derecho."
#define DOOM_FPS_LABEL_ES "Mostrar FPS"
#define DOOM_FPS_INFO_0_ES "Muestra un contador de velocidad de fotogramas en pantalla."

struct retro_core_option_v2_category option_cats_es[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_es[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_ES,
      NULL,
      DOOM_FRAMERATE_INFO_0_ES,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_ES},
         { "50",              OPTION_VAL_50_ES},
         { "60",              OPTION_VAL_60_ES},
         { "72",              OPTION_VAL_72_ES},
         { "75",              OPTION_VAL_75_ES},
         { "90",              OPTION_VAL_90_ES},
         { "100",              OPTION_VAL_100_ES},
         { "119",              OPTION_VAL_119_ES},
         { "120",              OPTION_VAL_120_ES},
         { "144",              OPTION_VAL_144_ES},
         { "155",              OPTION_VAL_155_ES},
         { "160",              OPTION_VAL_160_ES},
         { "165",              OPTION_VAL_165_ES},
         { "180",              OPTION_VAL_180_ES},
         { "200",              OPTION_VAL_200_ES},
         { "240",              OPTION_VAL_240_ES},
         { "244",              OPTION_VAL_244_ES},
         { "300",              OPTION_VAL_300_ES},
         { "360",              OPTION_VAL_360_ES},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_ES,
      NULL,
      DOOM_RESOLUTION_INFO_0_ES,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_ES },
         { "640x368",   OPTION_VAL_640X368_ES },
         { "720x408",   OPTION_VAL_720X408_ES },
         { "960x544",   OPTION_VAL_960X544_ES },
		 { "1280x720",   OPTION_VAL_1280X720_ES },
		 { "1920x1080",   OPTION_VAL_1920X1080_ES },
		 { "2560x1440",   OPTION_VAL_2560X1440_ES },
		 { "3840x2160",   OPTION_VAL_3840X2160_ES },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_ES,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_ES,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_ES,
      NULL,
      DOOM_FPS_INFO_0_ES,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_es = {
   option_cats_es,
   option_defs_es
};

/* RETRO_LANGUAGE_FA */

#define DOOM_FRAMERATE_LABEL_FA NULL
#define DOOM_FRAMERATE_INFO_0_FA NULL
#define OPTION_VAL_AUTO_FA "خودکار"
#define OPTION_VAL_50_FA NULL
#define OPTION_VAL_60_FA NULL
#define OPTION_VAL_72_FA NULL
#define OPTION_VAL_75_FA NULL
#define OPTION_VAL_90_FA NULL
#define OPTION_VAL_100_FA NULL
#define OPTION_VAL_119_FA NULL
#define OPTION_VAL_120_FA NULL
#define OPTION_VAL_144_FA NULL
#define OPTION_VAL_155_FA NULL
#define OPTION_VAL_160_FA NULL
#define OPTION_VAL_165_FA NULL
#define OPTION_VAL_180_FA NULL
#define OPTION_VAL_200_FA NULL
#define OPTION_VAL_240_FA NULL
#define OPTION_VAL_244_FA NULL
#define OPTION_VAL_300_FA NULL
#define OPTION_VAL_360_FA NULL
#define DOOM_RESOLUTION_LABEL_FA NULL
#define DOOM_RESOLUTION_INFO_0_FA NULL
#define OPTION_VAL_480X272_FA NULL
#define OPTION_VAL_640X368_FA NULL
#define OPTION_VAL_720X408_FA NULL
#define OPTION_VAL_960X544_FA NULL
#define OPTION_VAL_1280X720_FA NULL
#define OPTION_VAL_1920X1080_FA NULL
#define OPTION_VAL_2560X1440_FA NULL
#define OPTION_VAL_3840X2160_FA NULL
#define DOOM_INVERT_Y_AXIS_LABEL_FA NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_FA NULL
#define DOOM_FPS_LABEL_FA NULL
#define DOOM_FPS_INFO_0_FA NULL

struct retro_core_option_v2_category option_cats_fa[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_fa[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_FA,
      NULL,
      DOOM_FRAMERATE_INFO_0_FA,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_FA},
         { "50",              OPTION_VAL_50_FA},
         { "60",              OPTION_VAL_60_FA},
         { "72",              OPTION_VAL_72_FA},
         { "75",              OPTION_VAL_75_FA},
         { "90",              OPTION_VAL_90_FA},
         { "100",              OPTION_VAL_100_FA},
         { "119",              OPTION_VAL_119_FA},
         { "120",              OPTION_VAL_120_FA},
         { "144",              OPTION_VAL_144_FA},
         { "155",              OPTION_VAL_155_FA},
         { "160",              OPTION_VAL_160_FA},
         { "165",              OPTION_VAL_165_FA},
         { "180",              OPTION_VAL_180_FA},
         { "200",              OPTION_VAL_200_FA},
         { "240",              OPTION_VAL_240_FA},
         { "244",              OPTION_VAL_244_FA},
         { "300",              OPTION_VAL_300_FA},
         { "360",              OPTION_VAL_360_FA},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_FA,
      NULL,
      DOOM_RESOLUTION_INFO_0_FA,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_FA },
         { "640x368",   OPTION_VAL_640X368_FA },
         { "720x408",   OPTION_VAL_720X408_FA },
         { "960x544",   OPTION_VAL_960X544_FA },
		 { "1280x720",   OPTION_VAL_1280X720_FA },
		 { "1920x1080",   OPTION_VAL_1920X1080_FA },
		 { "2560x1440",   OPTION_VAL_2560X1440_FA },
		 { "3840x2160",   OPTION_VAL_3840X2160_FA },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_FA,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_FA,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_FA,
      NULL,
      DOOM_FPS_INFO_0_FA,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_fa = {
   option_cats_fa,
   option_defs_fa
};

/* RETRO_LANGUAGE_FI */

#define DOOM_FRAMERATE_LABEL_FI "Kuvataajuus (Uudelleenkäynnistys vaaditaan)"
#define DOOM_FRAMERATE_INFO_0_FI "Valitse haluttu kuvataajuus."
#define OPTION_VAL_AUTO_FI "Automaattinen"
#define OPTION_VAL_50_FI "50 fps"
#define OPTION_VAL_60_FI "60 fps"
#define OPTION_VAL_72_FI "72 fps"
#define OPTION_VAL_75_FI "75 fps"
#define OPTION_VAL_90_FI "90 fps"
#define OPTION_VAL_100_FI "100 fps"
#define OPTION_VAL_119_FI "119 fps"
#define OPTION_VAL_120_FI "120 fps"
#define OPTION_VAL_144_FI "144 fps"
#define OPTION_VAL_155_FI "155 fps"
#define OPTION_VAL_160_FI "160 fps"
#define OPTION_VAL_165_FI "165 fps"
#define OPTION_VAL_180_FI "180 fps"
#define OPTION_VAL_200_FI "200 fps"
#define OPTION_VAL_240_FI "240 fps"
#define OPTION_VAL_244_FI "244 fps"
#define OPTION_VAL_300_FI "300 fps"
#define OPTION_VAL_360_FI "360 fps"
#define DOOM_RESOLUTION_LABEL_FI "Sisäinen resoluutio (Uudelleenkäynnistys vaaditaan)"
#define DOOM_RESOLUTION_INFO_0_FI "Valitse haluttu resoluutio."
#define OPTION_VAL_480X272_FI "480 x 272"
#define OPTION_VAL_640X368_FI "640 x 368"
#define OPTION_VAL_720X408_FI "720 x 408"
#define OPTION_VAL_960X544_FI "960 x 544 (Oletus)"
#define OPTION_VAL_1280X720_FI "1280 x 720"
#define OPTION_VAL_1920X1080_FI "1920 x 1080"
#define OPTION_VAL_2560X1440_FI "2560 x 1440"
#define OPTION_VAL_3840X2160_FI "3840 x 2160"
#define DOOM_INVERT_Y_AXIS_LABEL_FI "Käännä Y-akseli"
#define DOOM_INVERT_Y_AXIS_INFO_0_FI "Käännä oikean analogisen sauvan Y-akseli."
#define DOOM_FPS_LABEL_FI "Näytä kuvataajuus"
#define DOOM_FPS_INFO_0_FI "Näytä kuvataajuus näytöllä."

struct retro_core_option_v2_category option_cats_fi[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_fi[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_FI,
      NULL,
      DOOM_FRAMERATE_INFO_0_FI,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_FI},
         { "50",              OPTION_VAL_50_FI},
         { "60",              OPTION_VAL_60_FI},
         { "72",              OPTION_VAL_72_FI},
         { "75",              OPTION_VAL_75_FI},
         { "90",              OPTION_VAL_90_FI},
         { "100",              OPTION_VAL_100_FI},
         { "119",              OPTION_VAL_119_FI},
         { "120",              OPTION_VAL_120_FI},
         { "144",              OPTION_VAL_144_FI},
         { "155",              OPTION_VAL_155_FI},
         { "160",              OPTION_VAL_160_FI},
         { "165",              OPTION_VAL_165_FI},
         { "180",              OPTION_VAL_180_FI},
         { "200",              OPTION_VAL_200_FI},
         { "240",              OPTION_VAL_240_FI},
         { "244",              OPTION_VAL_244_FI},
         { "300",              OPTION_VAL_300_FI},
         { "360",              OPTION_VAL_360_FI},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_FI,
      NULL,
      DOOM_RESOLUTION_INFO_0_FI,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_FI },
         { "640x368",   OPTION_VAL_640X368_FI },
         { "720x408",   OPTION_VAL_720X408_FI },
         { "960x544",   OPTION_VAL_960X544_FI },
		 { "1280x720",   OPTION_VAL_1280X720_FI },
		 { "1920x1080",   OPTION_VAL_1920X1080_FI },
		 { "2560x1440",   OPTION_VAL_2560X1440_FI },
		 { "3840x2160",   OPTION_VAL_3840X2160_FI },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_FI,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_FI,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_FI,
      NULL,
      DOOM_FPS_INFO_0_FI,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_fi = {
   option_cats_fi,
   option_defs_fi
};

/* RETRO_LANGUAGE_FR */

#define DOOM_FRAMERATE_LABEL_FR "Fréquence d'images (Redémarrage requis)"
#define DOOM_FRAMERATE_INFO_0_FR "Choisissez la fréquence d'images souhaitée."
#define OPTION_VAL_AUTO_FR NULL
#define OPTION_VAL_50_FR "50i/s"
#define OPTION_VAL_60_FR "60i/s"
#define OPTION_VAL_72_FR "72i/s"
#define OPTION_VAL_75_FR "75i/s"
#define OPTION_VAL_90_FR "90i/s"
#define OPTION_VAL_100_FR "100i/s"
#define OPTION_VAL_119_FR "119i/s"
#define OPTION_VAL_120_FR "120i/s"
#define OPTION_VAL_144_FR "144i/s"
#define OPTION_VAL_155_FR "155i/s"
#define OPTION_VAL_160_FR "160i/s"
#define OPTION_VAL_165_FR "165i/s"
#define OPTION_VAL_180_FR "180i/s"
#define OPTION_VAL_200_FR "200i/s"
#define OPTION_VAL_240_FR "240i/s"
#define OPTION_VAL_244_FR "244i/s"
#define OPTION_VAL_300_FR "300i/s"
#define OPTION_VAL_360_FR "360i/s"
#define DOOM_RESOLUTION_LABEL_FR "Résolution interne (Redémarrage requis)"
#define DOOM_RESOLUTION_INFO_0_FR "Choisissez la résolution à afficher."
#define OPTION_VAL_480X272_FR NULL
#define OPTION_VAL_640X368_FR NULL
#define OPTION_VAL_720X408_FR NULL
#define OPTION_VAL_960X544_FR "960x544 (par défaut)"
#define OPTION_VAL_1280X720_FR NULL
#define OPTION_VAL_1920X1080_FR NULL
#define OPTION_VAL_2560X1440_FR NULL
#define OPTION_VAL_3840X2160_FR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_FR "Inverser l'axe Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_FR "Inverser l'axe Y du stick analogique droit."
#define DOOM_FPS_LABEL_FR "Afficher les images/s"
#define DOOM_FPS_INFO_0_FR "Afficher la fréquence d'images à l'écran."

struct retro_core_option_v2_category option_cats_fr[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_fr[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_FR,
      NULL,
      DOOM_FRAMERATE_INFO_0_FR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_FR},
         { "50",              OPTION_VAL_50_FR},
         { "60",              OPTION_VAL_60_FR},
         { "72",              OPTION_VAL_72_FR},
         { "75",              OPTION_VAL_75_FR},
         { "90",              OPTION_VAL_90_FR},
         { "100",              OPTION_VAL_100_FR},
         { "119",              OPTION_VAL_119_FR},
         { "120",              OPTION_VAL_120_FR},
         { "144",              OPTION_VAL_144_FR},
         { "155",              OPTION_VAL_155_FR},
         { "160",              OPTION_VAL_160_FR},
         { "165",              OPTION_VAL_165_FR},
         { "180",              OPTION_VAL_180_FR},
         { "200",              OPTION_VAL_200_FR},
         { "240",              OPTION_VAL_240_FR},
         { "244",              OPTION_VAL_244_FR},
         { "300",              OPTION_VAL_300_FR},
         { "360",              OPTION_VAL_360_FR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_FR,
      NULL,
      DOOM_RESOLUTION_INFO_0_FR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_FR },
         { "640x368",   OPTION_VAL_640X368_FR },
         { "720x408",   OPTION_VAL_720X408_FR },
         { "960x544",   OPTION_VAL_960X544_FR },
		 { "1280x720",   OPTION_VAL_1280X720_FR },
		 { "1920x1080",   OPTION_VAL_1920X1080_FR },
		 { "2560x1440",   OPTION_VAL_2560X1440_FR },
		 { "3840x2160",   OPTION_VAL_3840X2160_FR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_FR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_FR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_FR,
      NULL,
      DOOM_FPS_INFO_0_FR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_fr = {
   option_cats_fr,
   option_defs_fr
};

/* RETRO_LANGUAGE_GL */

#define DOOM_FRAMERATE_LABEL_GL NULL
#define DOOM_FRAMERATE_INFO_0_GL NULL
#define OPTION_VAL_AUTO_GL NULL
#define OPTION_VAL_50_GL NULL
#define OPTION_VAL_60_GL NULL
#define OPTION_VAL_72_GL NULL
#define OPTION_VAL_75_GL NULL
#define OPTION_VAL_90_GL NULL
#define OPTION_VAL_100_GL NULL
#define OPTION_VAL_119_GL NULL
#define OPTION_VAL_120_GL NULL
#define OPTION_VAL_144_GL NULL
#define OPTION_VAL_155_GL NULL
#define OPTION_VAL_160_GL NULL
#define OPTION_VAL_165_GL NULL
#define OPTION_VAL_180_GL NULL
#define OPTION_VAL_200_GL NULL
#define OPTION_VAL_240_GL NULL
#define OPTION_VAL_244_GL NULL
#define OPTION_VAL_300_GL NULL
#define OPTION_VAL_360_GL NULL
#define DOOM_RESOLUTION_LABEL_GL NULL
#define DOOM_RESOLUTION_INFO_0_GL NULL
#define OPTION_VAL_480X272_GL NULL
#define OPTION_VAL_640X368_GL NULL
#define OPTION_VAL_720X408_GL NULL
#define OPTION_VAL_960X544_GL NULL
#define OPTION_VAL_1280X720_GL NULL
#define OPTION_VAL_1920X1080_GL NULL
#define OPTION_VAL_2560X1440_GL NULL
#define OPTION_VAL_3840X2160_GL NULL
#define DOOM_INVERT_Y_AXIS_LABEL_GL NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_GL NULL
#define DOOM_FPS_LABEL_GL NULL
#define DOOM_FPS_INFO_0_GL NULL

struct retro_core_option_v2_category option_cats_gl[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_gl[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_GL,
      NULL,
      DOOM_FRAMERATE_INFO_0_GL,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_GL},
         { "50",              OPTION_VAL_50_GL},
         { "60",              OPTION_VAL_60_GL},
         { "72",              OPTION_VAL_72_GL},
         { "75",              OPTION_VAL_75_GL},
         { "90",              OPTION_VAL_90_GL},
         { "100",              OPTION_VAL_100_GL},
         { "119",              OPTION_VAL_119_GL},
         { "120",              OPTION_VAL_120_GL},
         { "144",              OPTION_VAL_144_GL},
         { "155",              OPTION_VAL_155_GL},
         { "160",              OPTION_VAL_160_GL},
         { "165",              OPTION_VAL_165_GL},
         { "180",              OPTION_VAL_180_GL},
         { "200",              OPTION_VAL_200_GL},
         { "240",              OPTION_VAL_240_GL},
         { "244",              OPTION_VAL_244_GL},
         { "300",              OPTION_VAL_300_GL},
         { "360",              OPTION_VAL_360_GL},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_GL,
      NULL,
      DOOM_RESOLUTION_INFO_0_GL,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_GL },
         { "640x368",   OPTION_VAL_640X368_GL },
         { "720x408",   OPTION_VAL_720X408_GL },
         { "960x544",   OPTION_VAL_960X544_GL },
		 { "1280x720",   OPTION_VAL_1280X720_GL },
		 { "1920x1080",   OPTION_VAL_1920X1080_GL },
		 { "2560x1440",   OPTION_VAL_2560X1440_GL },
		 { "3840x2160",   OPTION_VAL_3840X2160_GL },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_GL,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_GL,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_GL,
      NULL,
      DOOM_FPS_INFO_0_GL,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_gl = {
   option_cats_gl,
   option_defs_gl
};

/* RETRO_LANGUAGE_HE */

#define DOOM_FRAMERATE_LABEL_HE NULL
#define DOOM_FRAMERATE_INFO_0_HE NULL
#define OPTION_VAL_AUTO_HE NULL
#define OPTION_VAL_50_HE NULL
#define OPTION_VAL_60_HE NULL
#define OPTION_VAL_72_HE NULL
#define OPTION_VAL_75_HE NULL
#define OPTION_VAL_90_HE NULL
#define OPTION_VAL_100_HE NULL
#define OPTION_VAL_119_HE NULL
#define OPTION_VAL_120_HE NULL
#define OPTION_VAL_144_HE NULL
#define OPTION_VAL_155_HE NULL
#define OPTION_VAL_160_HE NULL
#define OPTION_VAL_165_HE NULL
#define OPTION_VAL_180_HE NULL
#define OPTION_VAL_200_HE NULL
#define OPTION_VAL_240_HE NULL
#define OPTION_VAL_244_HE NULL
#define OPTION_VAL_300_HE NULL
#define OPTION_VAL_360_HE NULL
#define DOOM_RESOLUTION_LABEL_HE NULL
#define DOOM_RESOLUTION_INFO_0_HE NULL
#define OPTION_VAL_480X272_HE NULL
#define OPTION_VAL_640X368_HE NULL
#define OPTION_VAL_720X408_HE NULL
#define OPTION_VAL_960X544_HE NULL
#define OPTION_VAL_1280X720_HE NULL
#define OPTION_VAL_1920X1080_HE NULL
#define OPTION_VAL_2560X1440_HE NULL
#define OPTION_VAL_3840X2160_HE NULL
#define DOOM_INVERT_Y_AXIS_LABEL_HE NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_HE NULL
#define DOOM_FPS_LABEL_HE NULL
#define DOOM_FPS_INFO_0_HE NULL

struct retro_core_option_v2_category option_cats_he[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_he[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_HE,
      NULL,
      DOOM_FRAMERATE_INFO_0_HE,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_HE},
         { "50",              OPTION_VAL_50_HE},
         { "60",              OPTION_VAL_60_HE},
         { "72",              OPTION_VAL_72_HE},
         { "75",              OPTION_VAL_75_HE},
         { "90",              OPTION_VAL_90_HE},
         { "100",              OPTION_VAL_100_HE},
         { "119",              OPTION_VAL_119_HE},
         { "120",              OPTION_VAL_120_HE},
         { "144",              OPTION_VAL_144_HE},
         { "155",              OPTION_VAL_155_HE},
         { "160",              OPTION_VAL_160_HE},
         { "165",              OPTION_VAL_165_HE},
         { "180",              OPTION_VAL_180_HE},
         { "200",              OPTION_VAL_200_HE},
         { "240",              OPTION_VAL_240_HE},
         { "244",              OPTION_VAL_244_HE},
         { "300",              OPTION_VAL_300_HE},
         { "360",              OPTION_VAL_360_HE},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_HE,
      NULL,
      DOOM_RESOLUTION_INFO_0_HE,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_HE },
         { "640x368",   OPTION_VAL_640X368_HE },
         { "720x408",   OPTION_VAL_720X408_HE },
         { "960x544",   OPTION_VAL_960X544_HE },
		 { "1280x720",   OPTION_VAL_1280X720_HE },
		 { "1920x1080",   OPTION_VAL_1920X1080_HE },
		 { "2560x1440",   OPTION_VAL_2560X1440_HE },
		 { "3840x2160",   OPTION_VAL_3840X2160_HE },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_HE,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_HE,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_HE,
      NULL,
      DOOM_FPS_INFO_0_HE,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_he = {
   option_cats_he,
   option_defs_he
};

/* RETRO_LANGUAGE_HR */

#define DOOM_FRAMERATE_LABEL_HR NULL
#define DOOM_FRAMERATE_INFO_0_HR NULL
#define OPTION_VAL_AUTO_HR NULL
#define OPTION_VAL_50_HR NULL
#define OPTION_VAL_60_HR NULL
#define OPTION_VAL_72_HR NULL
#define OPTION_VAL_75_HR NULL
#define OPTION_VAL_90_HR NULL
#define OPTION_VAL_100_HR NULL
#define OPTION_VAL_119_HR NULL
#define OPTION_VAL_120_HR NULL
#define OPTION_VAL_144_HR NULL
#define OPTION_VAL_155_HR NULL
#define OPTION_VAL_160_HR NULL
#define OPTION_VAL_165_HR NULL
#define OPTION_VAL_180_HR NULL
#define OPTION_VAL_200_HR NULL
#define OPTION_VAL_240_HR NULL
#define OPTION_VAL_244_HR NULL
#define OPTION_VAL_300_HR NULL
#define OPTION_VAL_360_HR NULL
#define DOOM_RESOLUTION_LABEL_HR NULL
#define DOOM_RESOLUTION_INFO_0_HR NULL
#define OPTION_VAL_480X272_HR NULL
#define OPTION_VAL_640X368_HR NULL
#define OPTION_VAL_720X408_HR NULL
#define OPTION_VAL_960X544_HR NULL
#define OPTION_VAL_1280X720_HR NULL
#define OPTION_VAL_1920X1080_HR NULL
#define OPTION_VAL_2560X1440_HR NULL
#define OPTION_VAL_3840X2160_HR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_HR NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_HR NULL
#define DOOM_FPS_LABEL_HR NULL
#define DOOM_FPS_INFO_0_HR NULL

struct retro_core_option_v2_category option_cats_hr[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_hr[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_HR,
      NULL,
      DOOM_FRAMERATE_INFO_0_HR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_HR},
         { "50",              OPTION_VAL_50_HR},
         { "60",              OPTION_VAL_60_HR},
         { "72",              OPTION_VAL_72_HR},
         { "75",              OPTION_VAL_75_HR},
         { "90",              OPTION_VAL_90_HR},
         { "100",              OPTION_VAL_100_HR},
         { "119",              OPTION_VAL_119_HR},
         { "120",              OPTION_VAL_120_HR},
         { "144",              OPTION_VAL_144_HR},
         { "155",              OPTION_VAL_155_HR},
         { "160",              OPTION_VAL_160_HR},
         { "165",              OPTION_VAL_165_HR},
         { "180",              OPTION_VAL_180_HR},
         { "200",              OPTION_VAL_200_HR},
         { "240",              OPTION_VAL_240_HR},
         { "244",              OPTION_VAL_244_HR},
         { "300",              OPTION_VAL_300_HR},
         { "360",              OPTION_VAL_360_HR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_HR,
      NULL,
      DOOM_RESOLUTION_INFO_0_HR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_HR },
         { "640x368",   OPTION_VAL_640X368_HR },
         { "720x408",   OPTION_VAL_720X408_HR },
         { "960x544",   OPTION_VAL_960X544_HR },
		 { "1280x720",   OPTION_VAL_1280X720_HR },
		 { "1920x1080",   OPTION_VAL_1920X1080_HR },
		 { "2560x1440",   OPTION_VAL_2560X1440_HR },
		 { "3840x2160",   OPTION_VAL_3840X2160_HR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_HR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_HR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_HR,
      NULL,
      DOOM_FPS_INFO_0_HR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_hr = {
   option_cats_hr,
   option_defs_hr
};

/* RETRO_LANGUAGE_HU */

#define DOOM_FRAMERATE_LABEL_HU "Képkockák száma másodpercenként (újraindítás szükséges)"
#define DOOM_FRAMERATE_INFO_0_HU "Az FPS kívánt értéke."
#define OPTION_VAL_AUTO_HU "Automatikus"
#define OPTION_VAL_50_HU "50 fps"
#define OPTION_VAL_60_HU "60 fps"
#define OPTION_VAL_72_HU "72 fps"
#define OPTION_VAL_75_HU "75 fps"
#define OPTION_VAL_90_HU "90 fps"
#define OPTION_VAL_100_HU "100 fps"
#define OPTION_VAL_119_HU "119 fps"
#define OPTION_VAL_120_HU "120 fps"
#define OPTION_VAL_144_HU "144 fps"
#define OPTION_VAL_155_HU "155 fps"
#define OPTION_VAL_160_HU "160 fps"
#define OPTION_VAL_165_HU "165 fps"
#define OPTION_VAL_180_HU "180 fps"
#define OPTION_VAL_200_HU "200 fps"
#define OPTION_VAL_240_HU "240 fps"
#define OPTION_VAL_244_HU "244 fps"
#define OPTION_VAL_300_HU "300 fps"
#define OPTION_VAL_360_HU "360 fps"
#define DOOM_RESOLUTION_LABEL_HU "Belső felbontás (újraindítás szükséges)"
#define DOOM_RESOLUTION_INFO_0_HU "Az előállított kép felbontása."
#define OPTION_VAL_480X272_HU NULL
#define OPTION_VAL_640X368_HU NULL
#define OPTION_VAL_720X408_HU NULL
#define OPTION_VAL_960X544_HU "960x544 (Alapértelmezett)"
#define OPTION_VAL_1280X720_HU NULL
#define OPTION_VAL_1920X1080_HU NULL
#define OPTION_VAL_2560X1440_HU NULL
#define OPTION_VAL_3840X2160_HU NULL
#define DOOM_INVERT_Y_AXIS_LABEL_HU "Fordított Y-tengely"
#define DOOM_INVERT_Y_AXIS_INFO_0_HU "A jobb analóg kar függőleges tengelyének megfordítása."
#define DOOM_FPS_LABEL_HU "FPS megjelenítése"
#define DOOM_FPS_INFO_0_HU "Az FPS érték megjelenítése a képernyőn."

struct retro_core_option_v2_category option_cats_hu[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_hu[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_HU,
      NULL,
      DOOM_FRAMERATE_INFO_0_HU,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_HU},
         { "50",              OPTION_VAL_50_HU},
         { "60",              OPTION_VAL_60_HU},
         { "72",              OPTION_VAL_72_HU},
         { "75",              OPTION_VAL_75_HU},
         { "90",              OPTION_VAL_90_HU},
         { "100",              OPTION_VAL_100_HU},
         { "119",              OPTION_VAL_119_HU},
         { "120",              OPTION_VAL_120_HU},
         { "144",              OPTION_VAL_144_HU},
         { "155",              OPTION_VAL_155_HU},
         { "160",              OPTION_VAL_160_HU},
         { "165",              OPTION_VAL_165_HU},
         { "180",              OPTION_VAL_180_HU},
         { "200",              OPTION_VAL_200_HU},
         { "240",              OPTION_VAL_240_HU},
         { "244",              OPTION_VAL_244_HU},
         { "300",              OPTION_VAL_300_HU},
         { "360",              OPTION_VAL_360_HU},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_HU,
      NULL,
      DOOM_RESOLUTION_INFO_0_HU,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_HU },
         { "640x368",   OPTION_VAL_640X368_HU },
         { "720x408",   OPTION_VAL_720X408_HU },
         { "960x544",   OPTION_VAL_960X544_HU },
		 { "1280x720",   OPTION_VAL_1280X720_HU },
		 { "1920x1080",   OPTION_VAL_1920X1080_HU },
		 { "2560x1440",   OPTION_VAL_2560X1440_HU },
		 { "3840x2160",   OPTION_VAL_3840X2160_HU },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_HU,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_HU,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_HU,
      NULL,
      DOOM_FPS_INFO_0_HU,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_hu = {
   option_cats_hu,
   option_defs_hu
};

/* RETRO_LANGUAGE_ID */

#define DOOM_FRAMERATE_LABEL_ID NULL
#define DOOM_FRAMERATE_INFO_0_ID NULL
#define OPTION_VAL_AUTO_ID "Otomatis"
#define OPTION_VAL_50_ID NULL
#define OPTION_VAL_60_ID NULL
#define OPTION_VAL_72_ID NULL
#define OPTION_VAL_75_ID NULL
#define OPTION_VAL_90_ID NULL
#define OPTION_VAL_100_ID NULL
#define OPTION_VAL_119_ID NULL
#define OPTION_VAL_120_ID NULL
#define OPTION_VAL_144_ID NULL
#define OPTION_VAL_155_ID NULL
#define OPTION_VAL_160_ID NULL
#define OPTION_VAL_165_ID NULL
#define OPTION_VAL_180_ID NULL
#define OPTION_VAL_200_ID NULL
#define OPTION_VAL_240_ID NULL
#define OPTION_VAL_244_ID NULL
#define OPTION_VAL_300_ID NULL
#define OPTION_VAL_360_ID NULL
#define DOOM_RESOLUTION_LABEL_ID NULL
#define DOOM_RESOLUTION_INFO_0_ID NULL
#define OPTION_VAL_480X272_ID NULL
#define OPTION_VAL_640X368_ID NULL
#define OPTION_VAL_720X408_ID NULL
#define OPTION_VAL_960X544_ID NULL
#define OPTION_VAL_1280X720_ID NULL
#define OPTION_VAL_1920X1080_ID NULL
#define OPTION_VAL_2560X1440_ID NULL
#define OPTION_VAL_3840X2160_ID NULL
#define DOOM_INVERT_Y_AXIS_LABEL_ID NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_ID NULL
#define DOOM_FPS_LABEL_ID NULL
#define DOOM_FPS_INFO_0_ID NULL

struct retro_core_option_v2_category option_cats_id[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_id[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_ID,
      NULL,
      DOOM_FRAMERATE_INFO_0_ID,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_ID},
         { "50",              OPTION_VAL_50_ID},
         { "60",              OPTION_VAL_60_ID},
         { "72",              OPTION_VAL_72_ID},
         { "75",              OPTION_VAL_75_ID},
         { "90",              OPTION_VAL_90_ID},
         { "100",              OPTION_VAL_100_ID},
         { "119",              OPTION_VAL_119_ID},
         { "120",              OPTION_VAL_120_ID},
         { "144",              OPTION_VAL_144_ID},
         { "155",              OPTION_VAL_155_ID},
         { "160",              OPTION_VAL_160_ID},
         { "165",              OPTION_VAL_165_ID},
         { "180",              OPTION_VAL_180_ID},
         { "200",              OPTION_VAL_200_ID},
         { "240",              OPTION_VAL_240_ID},
         { "244",              OPTION_VAL_244_ID},
         { "300",              OPTION_VAL_300_ID},
         { "360",              OPTION_VAL_360_ID},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_ID,
      NULL,
      DOOM_RESOLUTION_INFO_0_ID,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_ID },
         { "640x368",   OPTION_VAL_640X368_ID },
         { "720x408",   OPTION_VAL_720X408_ID },
         { "960x544",   OPTION_VAL_960X544_ID },
		 { "1280x720",   OPTION_VAL_1280X720_ID },
		 { "1920x1080",   OPTION_VAL_1920X1080_ID },
		 { "2560x1440",   OPTION_VAL_2560X1440_ID },
		 { "3840x2160",   OPTION_VAL_3840X2160_ID },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_ID,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_ID,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_ID,
      NULL,
      DOOM_FPS_INFO_0_ID,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_id = {
   option_cats_id,
   option_defs_id
};

/* RETRO_LANGUAGE_IT */

#define DOOM_FRAMERATE_LABEL_IT "Framerate (riavvio)"
#define DOOM_FRAMERATE_INFO_0_IT "Scegli il frame rate desiderato."
#define OPTION_VAL_AUTO_IT NULL
#define OPTION_VAL_50_IT NULL
#define OPTION_VAL_60_IT NULL
#define OPTION_VAL_72_IT NULL
#define OPTION_VAL_75_IT NULL
#define OPTION_VAL_90_IT NULL
#define OPTION_VAL_100_IT NULL
#define OPTION_VAL_119_IT NULL
#define OPTION_VAL_120_IT NULL
#define OPTION_VAL_144_IT NULL
#define OPTION_VAL_155_IT NULL
#define OPTION_VAL_160_IT NULL
#define OPTION_VAL_165_IT NULL
#define OPTION_VAL_180_IT NULL
#define OPTION_VAL_200_IT NULL
#define OPTION_VAL_240_IT NULL
#define OPTION_VAL_244_IT NULL
#define OPTION_VAL_300_IT NULL
#define OPTION_VAL_360_IT NULL
#define DOOM_RESOLUTION_LABEL_IT "Risoluzione interna (riavvio)"
#define DOOM_RESOLUTION_INFO_0_IT "Scegli la risoluzione in cui renderizzare."
#define OPTION_VAL_480X272_IT NULL
#define OPTION_VAL_640X368_IT NULL
#define OPTION_VAL_720X408_IT NULL
#define OPTION_VAL_960X544_IT "960x544 (Predefinito)"
#define OPTION_VAL_1280X720_IT NULL
#define OPTION_VAL_1920X1080_IT NULL
#define OPTION_VAL_2560X1440_IT NULL
#define OPTION_VAL_3840X2160_IT NULL
#define DOOM_INVERT_Y_AXIS_LABEL_IT "Inverti Asse Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_IT "Inverte l'asse Y dell'analogico destro."
#define DOOM_FPS_LABEL_IT "Mostra FPS"
#define DOOM_FPS_INFO_0_IT "Mostra frame rate sullo schermo."

struct retro_core_option_v2_category option_cats_it[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_it[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_IT,
      NULL,
      DOOM_FRAMERATE_INFO_0_IT,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_IT},
         { "50",              OPTION_VAL_50_IT},
         { "60",              OPTION_VAL_60_IT},
         { "72",              OPTION_VAL_72_IT},
         { "75",              OPTION_VAL_75_IT},
         { "90",              OPTION_VAL_90_IT},
         { "100",              OPTION_VAL_100_IT},
         { "119",              OPTION_VAL_119_IT},
         { "120",              OPTION_VAL_120_IT},
         { "144",              OPTION_VAL_144_IT},
         { "155",              OPTION_VAL_155_IT},
         { "160",              OPTION_VAL_160_IT},
         { "165",              OPTION_VAL_165_IT},
         { "180",              OPTION_VAL_180_IT},
         { "200",              OPTION_VAL_200_IT},
         { "240",              OPTION_VAL_240_IT},
         { "244",              OPTION_VAL_244_IT},
         { "300",              OPTION_VAL_300_IT},
         { "360",              OPTION_VAL_360_IT},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_IT,
      NULL,
      DOOM_RESOLUTION_INFO_0_IT,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_IT },
         { "640x368",   OPTION_VAL_640X368_IT },
         { "720x408",   OPTION_VAL_720X408_IT },
         { "960x544",   OPTION_VAL_960X544_IT },
		 { "1280x720",   OPTION_VAL_1280X720_IT },
		 { "1920x1080",   OPTION_VAL_1920X1080_IT },
		 { "2560x1440",   OPTION_VAL_2560X1440_IT },
		 { "3840x2160",   OPTION_VAL_3840X2160_IT },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_IT,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_IT,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_IT,
      NULL,
      DOOM_FPS_INFO_0_IT,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_it = {
   option_cats_it,
   option_defs_it
};

/* RETRO_LANGUAGE_JA */

#define DOOM_FRAMERATE_LABEL_JA NULL
#define DOOM_FRAMERATE_INFO_0_JA NULL
#define OPTION_VAL_AUTO_JA "自動"
#define OPTION_VAL_50_JA NULL
#define OPTION_VAL_60_JA NULL
#define OPTION_VAL_72_JA NULL
#define OPTION_VAL_75_JA NULL
#define OPTION_VAL_90_JA NULL
#define OPTION_VAL_100_JA NULL
#define OPTION_VAL_119_JA NULL
#define OPTION_VAL_120_JA NULL
#define OPTION_VAL_144_JA NULL
#define OPTION_VAL_155_JA NULL
#define OPTION_VAL_160_JA NULL
#define OPTION_VAL_165_JA NULL
#define OPTION_VAL_180_JA NULL
#define OPTION_VAL_200_JA NULL
#define OPTION_VAL_240_JA NULL
#define OPTION_VAL_244_JA NULL
#define OPTION_VAL_300_JA NULL
#define OPTION_VAL_360_JA NULL
#define DOOM_RESOLUTION_LABEL_JA NULL
#define DOOM_RESOLUTION_INFO_0_JA NULL
#define OPTION_VAL_480X272_JA NULL
#define OPTION_VAL_640X368_JA NULL
#define OPTION_VAL_720X408_JA NULL
#define OPTION_VAL_960X544_JA NULL
#define OPTION_VAL_1280X720_JA NULL
#define OPTION_VAL_1920X1080_JA NULL
#define OPTION_VAL_2560X1440_JA NULL
#define OPTION_VAL_3840X2160_JA NULL
#define DOOM_INVERT_Y_AXIS_LABEL_JA NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_JA NULL
#define DOOM_FPS_LABEL_JA NULL
#define DOOM_FPS_INFO_0_JA NULL

struct retro_core_option_v2_category option_cats_ja[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ja[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_JA,
      NULL,
      DOOM_FRAMERATE_INFO_0_JA,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_JA},
         { "50",              OPTION_VAL_50_JA},
         { "60",              OPTION_VAL_60_JA},
         { "72",              OPTION_VAL_72_JA},
         { "75",              OPTION_VAL_75_JA},
         { "90",              OPTION_VAL_90_JA},
         { "100",              OPTION_VAL_100_JA},
         { "119",              OPTION_VAL_119_JA},
         { "120",              OPTION_VAL_120_JA},
         { "144",              OPTION_VAL_144_JA},
         { "155",              OPTION_VAL_155_JA},
         { "160",              OPTION_VAL_160_JA},
         { "165",              OPTION_VAL_165_JA},
         { "180",              OPTION_VAL_180_JA},
         { "200",              OPTION_VAL_200_JA},
         { "240",              OPTION_VAL_240_JA},
         { "244",              OPTION_VAL_244_JA},
         { "300",              OPTION_VAL_300_JA},
         { "360",              OPTION_VAL_360_JA},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_JA,
      NULL,
      DOOM_RESOLUTION_INFO_0_JA,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_JA },
         { "640x368",   OPTION_VAL_640X368_JA },
         { "720x408",   OPTION_VAL_720X408_JA },
         { "960x544",   OPTION_VAL_960X544_JA },
		 { "1280x720",   OPTION_VAL_1280X720_JA },
		 { "1920x1080",   OPTION_VAL_1920X1080_JA },
		 { "2560x1440",   OPTION_VAL_2560X1440_JA },
		 { "3840x2160",   OPTION_VAL_3840X2160_JA },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_JA,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_JA,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_JA,
      NULL,
      DOOM_FPS_INFO_0_JA,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ja = {
   option_cats_ja,
   option_defs_ja
};

/* RETRO_LANGUAGE_KO */

#define DOOM_FRAMERATE_LABEL_KO "프레임 레이트 (재시작 필요)"
#define DOOM_FRAMERATE_INFO_0_KO "원하는 프레임 레이트를 선택합니다."
#define OPTION_VAL_AUTO_KO "자동"
#define OPTION_VAL_50_KO NULL
#define OPTION_VAL_60_KO NULL
#define OPTION_VAL_72_KO NULL
#define OPTION_VAL_75_KO NULL
#define OPTION_VAL_90_KO NULL
#define OPTION_VAL_100_KO NULL
#define OPTION_VAL_119_KO NULL
#define OPTION_VAL_120_KO NULL
#define OPTION_VAL_144_KO NULL
#define OPTION_VAL_155_KO NULL
#define OPTION_VAL_160_KO NULL
#define OPTION_VAL_165_KO NULL
#define OPTION_VAL_180_KO NULL
#define OPTION_VAL_200_KO NULL
#define OPTION_VAL_240_KO NULL
#define OPTION_VAL_244_KO NULL
#define OPTION_VAL_300_KO NULL
#define OPTION_VAL_360_KO NULL
#define DOOM_RESOLUTION_LABEL_KO "내부 해상도 (재시작 필요)"
#define DOOM_RESOLUTION_INFO_0_KO "원하는 렌더링할 해상도를 선택합니다."
#define OPTION_VAL_480X272_KO NULL
#define OPTION_VAL_640X368_KO NULL
#define OPTION_VAL_720X408_KO NULL
#define OPTION_VAL_960X544_KO "960x544 (기본)"
#define OPTION_VAL_1280X720_KO NULL
#define OPTION_VAL_1920X1080_KO NULL
#define OPTION_VAL_2560X1440_KO NULL
#define OPTION_VAL_3840X2160_KO NULL
#define DOOM_INVERT_Y_AXIS_LABEL_KO "Y축 반전"
#define DOOM_INVERT_Y_AXIS_INFO_0_KO "오른쪽 아날로그 스틱의 Y축을 반전시킵니다."
#define DOOM_FPS_LABEL_KO "FPS 표시"
#define DOOM_FPS_INFO_0_KO "화면에 프레임 레이트를 표시합니다."

struct retro_core_option_v2_category option_cats_ko[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ko[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_KO,
      NULL,
      DOOM_FRAMERATE_INFO_0_KO,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_KO},
         { "50",              OPTION_VAL_50_KO},
         { "60",              OPTION_VAL_60_KO},
         { "72",              OPTION_VAL_72_KO},
         { "75",              OPTION_VAL_75_KO},
         { "90",              OPTION_VAL_90_KO},
         { "100",              OPTION_VAL_100_KO},
         { "119",              OPTION_VAL_119_KO},
         { "120",              OPTION_VAL_120_KO},
         { "144",              OPTION_VAL_144_KO},
         { "155",              OPTION_VAL_155_KO},
         { "160",              OPTION_VAL_160_KO},
         { "165",              OPTION_VAL_165_KO},
         { "180",              OPTION_VAL_180_KO},
         { "200",              OPTION_VAL_200_KO},
         { "240",              OPTION_VAL_240_KO},
         { "244",              OPTION_VAL_244_KO},
         { "300",              OPTION_VAL_300_KO},
         { "360",              OPTION_VAL_360_KO},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_KO,
      NULL,
      DOOM_RESOLUTION_INFO_0_KO,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_KO },
         { "640x368",   OPTION_VAL_640X368_KO },
         { "720x408",   OPTION_VAL_720X408_KO },
         { "960x544",   OPTION_VAL_960X544_KO },
		 { "1280x720",   OPTION_VAL_1280X720_KO },
		 { "1920x1080",   OPTION_VAL_1920X1080_KO },
		 { "2560x1440",   OPTION_VAL_2560X1440_KO },
		 { "3840x2160",   OPTION_VAL_3840X2160_KO },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_KO,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_KO,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_KO,
      NULL,
      DOOM_FPS_INFO_0_KO,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ko = {
   option_cats_ko,
   option_defs_ko
};

/* RETRO_LANGUAGE_MT */

#define DOOM_FRAMERATE_LABEL_MT NULL
#define DOOM_FRAMERATE_INFO_0_MT NULL
#define OPTION_VAL_AUTO_MT NULL
#define OPTION_VAL_50_MT NULL
#define OPTION_VAL_60_MT NULL
#define OPTION_VAL_72_MT NULL
#define OPTION_VAL_75_MT NULL
#define OPTION_VAL_90_MT NULL
#define OPTION_VAL_100_MT NULL
#define OPTION_VAL_119_MT NULL
#define OPTION_VAL_120_MT NULL
#define OPTION_VAL_144_MT NULL
#define OPTION_VAL_155_MT NULL
#define OPTION_VAL_160_MT NULL
#define OPTION_VAL_165_MT NULL
#define OPTION_VAL_180_MT NULL
#define OPTION_VAL_200_MT NULL
#define OPTION_VAL_240_MT NULL
#define OPTION_VAL_244_MT NULL
#define OPTION_VAL_300_MT NULL
#define OPTION_VAL_360_MT NULL
#define DOOM_RESOLUTION_LABEL_MT NULL
#define DOOM_RESOLUTION_INFO_0_MT NULL
#define OPTION_VAL_480X272_MT NULL
#define OPTION_VAL_640X368_MT NULL
#define OPTION_VAL_720X408_MT NULL
#define OPTION_VAL_960X544_MT NULL
#define OPTION_VAL_1280X720_MT NULL
#define OPTION_VAL_1920X1080_MT NULL
#define OPTION_VAL_2560X1440_MT NULL
#define OPTION_VAL_3840X2160_MT NULL
#define DOOM_INVERT_Y_AXIS_LABEL_MT NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_MT NULL
#define DOOM_FPS_LABEL_MT NULL
#define DOOM_FPS_INFO_0_MT NULL

struct retro_core_option_v2_category option_cats_mt[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_mt[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_MT,
      NULL,
      DOOM_FRAMERATE_INFO_0_MT,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_MT},
         { "50",              OPTION_VAL_50_MT},
         { "60",              OPTION_VAL_60_MT},
         { "72",              OPTION_VAL_72_MT},
         { "75",              OPTION_VAL_75_MT},
         { "90",              OPTION_VAL_90_MT},
         { "100",              OPTION_VAL_100_MT},
         { "119",              OPTION_VAL_119_MT},
         { "120",              OPTION_VAL_120_MT},
         { "144",              OPTION_VAL_144_MT},
         { "155",              OPTION_VAL_155_MT},
         { "160",              OPTION_VAL_160_MT},
         { "165",              OPTION_VAL_165_MT},
         { "180",              OPTION_VAL_180_MT},
         { "200",              OPTION_VAL_200_MT},
         { "240",              OPTION_VAL_240_MT},
         { "244",              OPTION_VAL_244_MT},
         { "300",              OPTION_VAL_300_MT},
         { "360",              OPTION_VAL_360_MT},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_MT,
      NULL,
      DOOM_RESOLUTION_INFO_0_MT,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_MT },
         { "640x368",   OPTION_VAL_640X368_MT },
         { "720x408",   OPTION_VAL_720X408_MT },
         { "960x544",   OPTION_VAL_960X544_MT },
		 { "1280x720",   OPTION_VAL_1280X720_MT },
		 { "1920x1080",   OPTION_VAL_1920X1080_MT },
		 { "2560x1440",   OPTION_VAL_2560X1440_MT },
		 { "3840x2160",   OPTION_VAL_3840X2160_MT },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_MT,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_MT,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_MT,
      NULL,
      DOOM_FPS_INFO_0_MT,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_mt = {
   option_cats_mt,
   option_defs_mt
};

/* RETRO_LANGUAGE_NL */

#define DOOM_FRAMERATE_LABEL_NL NULL
#define DOOM_FRAMERATE_INFO_0_NL NULL
#define OPTION_VAL_AUTO_NL "Automatisch"
#define OPTION_VAL_50_NL NULL
#define OPTION_VAL_60_NL NULL
#define OPTION_VAL_72_NL NULL
#define OPTION_VAL_75_NL NULL
#define OPTION_VAL_90_NL NULL
#define OPTION_VAL_100_NL NULL
#define OPTION_VAL_119_NL NULL
#define OPTION_VAL_120_NL NULL
#define OPTION_VAL_144_NL NULL
#define OPTION_VAL_155_NL NULL
#define OPTION_VAL_160_NL NULL
#define OPTION_VAL_165_NL NULL
#define OPTION_VAL_180_NL NULL
#define OPTION_VAL_200_NL NULL
#define OPTION_VAL_240_NL NULL
#define OPTION_VAL_244_NL NULL
#define OPTION_VAL_300_NL NULL
#define OPTION_VAL_360_NL NULL
#define DOOM_RESOLUTION_LABEL_NL NULL
#define DOOM_RESOLUTION_INFO_0_NL NULL
#define OPTION_VAL_480X272_NL NULL
#define OPTION_VAL_640X368_NL NULL
#define OPTION_VAL_720X408_NL NULL
#define OPTION_VAL_960X544_NL NULL
#define OPTION_VAL_1280X720_NL NULL
#define OPTION_VAL_1920X1080_NL NULL
#define OPTION_VAL_2560X1440_NL NULL
#define OPTION_VAL_3840X2160_NL NULL
#define DOOM_INVERT_Y_AXIS_LABEL_NL NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_NL NULL
#define DOOM_FPS_LABEL_NL NULL
#define DOOM_FPS_INFO_0_NL NULL

struct retro_core_option_v2_category option_cats_nl[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_nl[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_NL,
      NULL,
      DOOM_FRAMERATE_INFO_0_NL,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_NL},
         { "50",              OPTION_VAL_50_NL},
         { "60",              OPTION_VAL_60_NL},
         { "72",              OPTION_VAL_72_NL},
         { "75",              OPTION_VAL_75_NL},
         { "90",              OPTION_VAL_90_NL},
         { "100",              OPTION_VAL_100_NL},
         { "119",              OPTION_VAL_119_NL},
         { "120",              OPTION_VAL_120_NL},
         { "144",              OPTION_VAL_144_NL},
         { "155",              OPTION_VAL_155_NL},
         { "160",              OPTION_VAL_160_NL},
         { "165",              OPTION_VAL_165_NL},
         { "180",              OPTION_VAL_180_NL},
         { "200",              OPTION_VAL_200_NL},
         { "240",              OPTION_VAL_240_NL},
         { "244",              OPTION_VAL_244_NL},
         { "300",              OPTION_VAL_300_NL},
         { "360",              OPTION_VAL_360_NL},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_NL,
      NULL,
      DOOM_RESOLUTION_INFO_0_NL,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_NL },
         { "640x368",   OPTION_VAL_640X368_NL },
         { "720x408",   OPTION_VAL_720X408_NL },
         { "960x544",   OPTION_VAL_960X544_NL },
		 { "1280x720",   OPTION_VAL_1280X720_NL },
		 { "1920x1080",   OPTION_VAL_1920X1080_NL },
		 { "2560x1440",   OPTION_VAL_2560X1440_NL },
		 { "3840x2160",   OPTION_VAL_3840X2160_NL },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_NL,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_NL,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_NL,
      NULL,
      DOOM_FPS_INFO_0_NL,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_nl = {
   option_cats_nl,
   option_defs_nl
};

/* RETRO_LANGUAGE_NO */

#define DOOM_FRAMERATE_LABEL_NO NULL
#define DOOM_FRAMERATE_INFO_0_NO NULL
#define OPTION_VAL_AUTO_NO NULL
#define OPTION_VAL_50_NO NULL
#define OPTION_VAL_60_NO NULL
#define OPTION_VAL_72_NO NULL
#define OPTION_VAL_75_NO NULL
#define OPTION_VAL_90_NO NULL
#define OPTION_VAL_100_NO NULL
#define OPTION_VAL_119_NO NULL
#define OPTION_VAL_120_NO NULL
#define OPTION_VAL_144_NO NULL
#define OPTION_VAL_155_NO NULL
#define OPTION_VAL_160_NO NULL
#define OPTION_VAL_165_NO NULL
#define OPTION_VAL_180_NO NULL
#define OPTION_VAL_200_NO NULL
#define OPTION_VAL_240_NO NULL
#define OPTION_VAL_244_NO NULL
#define OPTION_VAL_300_NO NULL
#define OPTION_VAL_360_NO NULL
#define DOOM_RESOLUTION_LABEL_NO NULL
#define DOOM_RESOLUTION_INFO_0_NO NULL
#define OPTION_VAL_480X272_NO NULL
#define OPTION_VAL_640X368_NO NULL
#define OPTION_VAL_720X408_NO NULL
#define OPTION_VAL_960X544_NO NULL
#define OPTION_VAL_1280X720_NO NULL
#define OPTION_VAL_1920X1080_NO NULL
#define OPTION_VAL_2560X1440_NO NULL
#define OPTION_VAL_3840X2160_NO NULL
#define DOOM_INVERT_Y_AXIS_LABEL_NO NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_NO NULL
#define DOOM_FPS_LABEL_NO NULL
#define DOOM_FPS_INFO_0_NO NULL

struct retro_core_option_v2_category option_cats_no[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_no[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_NO,
      NULL,
      DOOM_FRAMERATE_INFO_0_NO,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_NO},
         { "50",              OPTION_VAL_50_NO},
         { "60",              OPTION_VAL_60_NO},
         { "72",              OPTION_VAL_72_NO},
         { "75",              OPTION_VAL_75_NO},
         { "90",              OPTION_VAL_90_NO},
         { "100",              OPTION_VAL_100_NO},
         { "119",              OPTION_VAL_119_NO},
         { "120",              OPTION_VAL_120_NO},
         { "144",              OPTION_VAL_144_NO},
         { "155",              OPTION_VAL_155_NO},
         { "160",              OPTION_VAL_160_NO},
         { "165",              OPTION_VAL_165_NO},
         { "180",              OPTION_VAL_180_NO},
         { "200",              OPTION_VAL_200_NO},
         { "240",              OPTION_VAL_240_NO},
         { "244",              OPTION_VAL_244_NO},
         { "300",              OPTION_VAL_300_NO},
         { "360",              OPTION_VAL_360_NO},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_NO,
      NULL,
      DOOM_RESOLUTION_INFO_0_NO,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_NO },
         { "640x368",   OPTION_VAL_640X368_NO },
         { "720x408",   OPTION_VAL_720X408_NO },
         { "960x544",   OPTION_VAL_960X544_NO },
		 { "1280x720",   OPTION_VAL_1280X720_NO },
		 { "1920x1080",   OPTION_VAL_1920X1080_NO },
		 { "2560x1440",   OPTION_VAL_2560X1440_NO },
		 { "3840x2160",   OPTION_VAL_3840X2160_NO },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_NO,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_NO,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_NO,
      NULL,
      DOOM_FPS_INFO_0_NO,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_no = {
   option_cats_no,
   option_defs_no
};

/* RETRO_LANGUAGE_OC */

#define DOOM_FRAMERATE_LABEL_OC NULL
#define DOOM_FRAMERATE_INFO_0_OC NULL
#define OPTION_VAL_AUTO_OC NULL
#define OPTION_VAL_50_OC NULL
#define OPTION_VAL_60_OC NULL
#define OPTION_VAL_72_OC NULL
#define OPTION_VAL_75_OC NULL
#define OPTION_VAL_90_OC NULL
#define OPTION_VAL_100_OC NULL
#define OPTION_VAL_119_OC NULL
#define OPTION_VAL_120_OC NULL
#define OPTION_VAL_144_OC NULL
#define OPTION_VAL_155_OC NULL
#define OPTION_VAL_160_OC NULL
#define OPTION_VAL_165_OC NULL
#define OPTION_VAL_180_OC NULL
#define OPTION_VAL_200_OC NULL
#define OPTION_VAL_240_OC NULL
#define OPTION_VAL_244_OC NULL
#define OPTION_VAL_300_OC NULL
#define OPTION_VAL_360_OC NULL
#define DOOM_RESOLUTION_LABEL_OC NULL
#define DOOM_RESOLUTION_INFO_0_OC NULL
#define OPTION_VAL_480X272_OC NULL
#define OPTION_VAL_640X368_OC NULL
#define OPTION_VAL_720X408_OC NULL
#define OPTION_VAL_960X544_OC NULL
#define OPTION_VAL_1280X720_OC NULL
#define OPTION_VAL_1920X1080_OC NULL
#define OPTION_VAL_2560X1440_OC NULL
#define OPTION_VAL_3840X2160_OC NULL
#define DOOM_INVERT_Y_AXIS_LABEL_OC NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_OC NULL
#define DOOM_FPS_LABEL_OC NULL
#define DOOM_FPS_INFO_0_OC NULL

struct retro_core_option_v2_category option_cats_oc[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_oc[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_OC,
      NULL,
      DOOM_FRAMERATE_INFO_0_OC,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_OC},
         { "50",              OPTION_VAL_50_OC},
         { "60",              OPTION_VAL_60_OC},
         { "72",              OPTION_VAL_72_OC},
         { "75",              OPTION_VAL_75_OC},
         { "90",              OPTION_VAL_90_OC},
         { "100",              OPTION_VAL_100_OC},
         { "119",              OPTION_VAL_119_OC},
         { "120",              OPTION_VAL_120_OC},
         { "144",              OPTION_VAL_144_OC},
         { "155",              OPTION_VAL_155_OC},
         { "160",              OPTION_VAL_160_OC},
         { "165",              OPTION_VAL_165_OC},
         { "180",              OPTION_VAL_180_OC},
         { "200",              OPTION_VAL_200_OC},
         { "240",              OPTION_VAL_240_OC},
         { "244",              OPTION_VAL_244_OC},
         { "300",              OPTION_VAL_300_OC},
         { "360",              OPTION_VAL_360_OC},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_OC,
      NULL,
      DOOM_RESOLUTION_INFO_0_OC,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_OC },
         { "640x368",   OPTION_VAL_640X368_OC },
         { "720x408",   OPTION_VAL_720X408_OC },
         { "960x544",   OPTION_VAL_960X544_OC },
		 { "1280x720",   OPTION_VAL_1280X720_OC },
		 { "1920x1080",   OPTION_VAL_1920X1080_OC },
		 { "2560x1440",   OPTION_VAL_2560X1440_OC },
		 { "3840x2160",   OPTION_VAL_3840X2160_OC },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_OC,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_OC,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_OC,
      NULL,
      DOOM_FPS_INFO_0_OC,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_oc = {
   option_cats_oc,
   option_defs_oc
};

/* RETRO_LANGUAGE_PL */

#define DOOM_FRAMERATE_LABEL_PL NULL
#define DOOM_FRAMERATE_INFO_0_PL NULL
#define OPTION_VAL_AUTO_PL NULL
#define OPTION_VAL_50_PL NULL
#define OPTION_VAL_60_PL NULL
#define OPTION_VAL_72_PL NULL
#define OPTION_VAL_75_PL NULL
#define OPTION_VAL_90_PL NULL
#define OPTION_VAL_100_PL NULL
#define OPTION_VAL_119_PL NULL
#define OPTION_VAL_120_PL NULL
#define OPTION_VAL_144_PL NULL
#define OPTION_VAL_155_PL NULL
#define OPTION_VAL_160_PL NULL
#define OPTION_VAL_165_PL NULL
#define OPTION_VAL_180_PL NULL
#define OPTION_VAL_200_PL NULL
#define OPTION_VAL_240_PL NULL
#define OPTION_VAL_244_PL NULL
#define OPTION_VAL_300_PL NULL
#define OPTION_VAL_360_PL NULL
#define DOOM_RESOLUTION_LABEL_PL NULL
#define DOOM_RESOLUTION_INFO_0_PL "Wybierz rozdzielczość do renderowania."
#define OPTION_VAL_480X272_PL NULL
#define OPTION_VAL_640X368_PL NULL
#define OPTION_VAL_720X408_PL NULL
#define OPTION_VAL_960X544_PL "960x544 (domyślnie)"
#define OPTION_VAL_1280X720_PL NULL
#define OPTION_VAL_1920X1080_PL NULL
#define OPTION_VAL_2560X1440_PL NULL
#define OPTION_VAL_3840X2160_PL NULL
#define DOOM_INVERT_Y_AXIS_LABEL_PL NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_PL NULL
#define DOOM_FPS_LABEL_PL NULL
#define DOOM_FPS_INFO_0_PL NULL

struct retro_core_option_v2_category option_cats_pl[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_pl[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_PL,
      NULL,
      DOOM_FRAMERATE_INFO_0_PL,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_PL},
         { "50",              OPTION_VAL_50_PL},
         { "60",              OPTION_VAL_60_PL},
         { "72",              OPTION_VAL_72_PL},
         { "75",              OPTION_VAL_75_PL},
         { "90",              OPTION_VAL_90_PL},
         { "100",              OPTION_VAL_100_PL},
         { "119",              OPTION_VAL_119_PL},
         { "120",              OPTION_VAL_120_PL},
         { "144",              OPTION_VAL_144_PL},
         { "155",              OPTION_VAL_155_PL},
         { "160",              OPTION_VAL_160_PL},
         { "165",              OPTION_VAL_165_PL},
         { "180",              OPTION_VAL_180_PL},
         { "200",              OPTION_VAL_200_PL},
         { "240",              OPTION_VAL_240_PL},
         { "244",              OPTION_VAL_244_PL},
         { "300",              OPTION_VAL_300_PL},
         { "360",              OPTION_VAL_360_PL},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_PL,
      NULL,
      DOOM_RESOLUTION_INFO_0_PL,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_PL },
         { "640x368",   OPTION_VAL_640X368_PL },
         { "720x408",   OPTION_VAL_720X408_PL },
         { "960x544",   OPTION_VAL_960X544_PL },
		 { "1280x720",   OPTION_VAL_1280X720_PL },
		 { "1920x1080",   OPTION_VAL_1920X1080_PL },
		 { "2560x1440",   OPTION_VAL_2560X1440_PL },
		 { "3840x2160",   OPTION_VAL_3840X2160_PL },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_PL,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_PL,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_PL,
      NULL,
      DOOM_FPS_INFO_0_PL,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_pl = {
   option_cats_pl,
   option_defs_pl
};

/* RETRO_LANGUAGE_PT_BR */

#define DOOM_FRAMERATE_LABEL_PT_BR "Tipo de quadros (requer reinício)"
#define DOOM_FRAMERATE_INFO_0_PT_BR "Define a taxa de quadros pretendida."
#define OPTION_VAL_AUTO_PT_BR "Automática"
#define OPTION_VAL_50_PT_BR NULL
#define OPTION_VAL_60_PT_BR NULL
#define OPTION_VAL_72_PT_BR NULL
#define OPTION_VAL_75_PT_BR NULL
#define OPTION_VAL_90_PT_BR NULL
#define OPTION_VAL_100_PT_BR NULL
#define OPTION_VAL_119_PT_BR NULL
#define OPTION_VAL_120_PT_BR NULL
#define OPTION_VAL_144_PT_BR NULL
#define OPTION_VAL_155_PT_BR NULL
#define OPTION_VAL_160_PT_BR NULL
#define OPTION_VAL_165_PT_BR NULL
#define OPTION_VAL_180_PT_BR NULL
#define OPTION_VAL_200_PT_BR NULL
#define OPTION_VAL_240_PT_BR NULL
#define OPTION_VAL_244_PT_BR NULL
#define OPTION_VAL_300_PT_BR NULL
#define OPTION_VAL_360_PT_BR NULL
#define DOOM_RESOLUTION_LABEL_PT_BR "Resolução interna (requer reinício)"
#define DOOM_RESOLUTION_INFO_0_PT_BR "Define a resolução a ser renderizada."
#define OPTION_VAL_480X272_PT_BR NULL
#define OPTION_VAL_640X368_PT_BR NULL
#define OPTION_VAL_720X408_PT_BR NULL
#define OPTION_VAL_960X544_PT_BR "960x544 (padrão)"
#define OPTION_VAL_1280X720_PT_BR NULL
#define OPTION_VAL_1920X1080_PT_BR NULL
#define OPTION_VAL_2560X1440_PT_BR NULL
#define OPTION_VAL_3840X2160_PT_BR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_PT_BR "Inverter eixo Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_PT_BR "Inverter o eixo Y do direcional analógico direito."
#define DOOM_FPS_LABEL_PT_BR "Mostrar FPS"
#define DOOM_FPS_INFO_0_PT_BR "Mostra a taxa de quadros na tela."

struct retro_core_option_v2_category option_cats_pt_br[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_pt_br[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_PT_BR,
      NULL,
      DOOM_FRAMERATE_INFO_0_PT_BR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_PT_BR},
         { "50",              OPTION_VAL_50_PT_BR},
         { "60",              OPTION_VAL_60_PT_BR},
         { "72",              OPTION_VAL_72_PT_BR},
         { "75",              OPTION_VAL_75_PT_BR},
         { "90",              OPTION_VAL_90_PT_BR},
         { "100",              OPTION_VAL_100_PT_BR},
         { "119",              OPTION_VAL_119_PT_BR},
         { "120",              OPTION_VAL_120_PT_BR},
         { "144",              OPTION_VAL_144_PT_BR},
         { "155",              OPTION_VAL_155_PT_BR},
         { "160",              OPTION_VAL_160_PT_BR},
         { "165",              OPTION_VAL_165_PT_BR},
         { "180",              OPTION_VAL_180_PT_BR},
         { "200",              OPTION_VAL_200_PT_BR},
         { "240",              OPTION_VAL_240_PT_BR},
         { "244",              OPTION_VAL_244_PT_BR},
         { "300",              OPTION_VAL_300_PT_BR},
         { "360",              OPTION_VAL_360_PT_BR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_PT_BR,
      NULL,
      DOOM_RESOLUTION_INFO_0_PT_BR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_PT_BR },
         { "640x368",   OPTION_VAL_640X368_PT_BR },
         { "720x408",   OPTION_VAL_720X408_PT_BR },
         { "960x544",   OPTION_VAL_960X544_PT_BR },
		 { "1280x720",   OPTION_VAL_1280X720_PT_BR },
		 { "1920x1080",   OPTION_VAL_1920X1080_PT_BR },
		 { "2560x1440",   OPTION_VAL_2560X1440_PT_BR },
		 { "3840x2160",   OPTION_VAL_3840X2160_PT_BR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_PT_BR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_PT_BR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_PT_BR,
      NULL,
      DOOM_FPS_INFO_0_PT_BR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_pt_br = {
   option_cats_pt_br,
   option_defs_pt_br
};

/* RETRO_LANGUAGE_PT_PT */

#define DOOM_FRAMERATE_LABEL_PT_PT NULL
#define DOOM_FRAMERATE_INFO_0_PT_PT NULL
#define OPTION_VAL_AUTO_PT_PT "Automático"
#define OPTION_VAL_50_PT_PT NULL
#define OPTION_VAL_60_PT_PT NULL
#define OPTION_VAL_72_PT_PT NULL
#define OPTION_VAL_75_PT_PT NULL
#define OPTION_VAL_90_PT_PT NULL
#define OPTION_VAL_100_PT_PT NULL
#define OPTION_VAL_119_PT_PT NULL
#define OPTION_VAL_120_PT_PT NULL
#define OPTION_VAL_144_PT_PT NULL
#define OPTION_VAL_155_PT_PT NULL
#define OPTION_VAL_160_PT_PT NULL
#define OPTION_VAL_165_PT_PT NULL
#define OPTION_VAL_180_PT_PT NULL
#define OPTION_VAL_200_PT_PT NULL
#define OPTION_VAL_240_PT_PT NULL
#define OPTION_VAL_244_PT_PT NULL
#define OPTION_VAL_300_PT_PT NULL
#define OPTION_VAL_360_PT_PT NULL
#define DOOM_RESOLUTION_LABEL_PT_PT NULL
#define DOOM_RESOLUTION_INFO_0_PT_PT NULL
#define OPTION_VAL_480X272_PT_PT NULL
#define OPTION_VAL_640X368_PT_PT NULL
#define OPTION_VAL_720X408_PT_PT NULL
#define OPTION_VAL_960X544_PT_PT NULL
#define OPTION_VAL_1280X720_PT_PT NULL
#define OPTION_VAL_1920X1080_PT_PT NULL
#define OPTION_VAL_2560X1440_PT_PT NULL
#define OPTION_VAL_3840X2160_PT_PT NULL
#define DOOM_INVERT_Y_AXIS_LABEL_PT_PT NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_PT_PT NULL
#define DOOM_FPS_LABEL_PT_PT NULL
#define DOOM_FPS_INFO_0_PT_PT NULL

struct retro_core_option_v2_category option_cats_pt_pt[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_pt_pt[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_PT_PT,
      NULL,
      DOOM_FRAMERATE_INFO_0_PT_PT,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_PT_PT},
         { "50",              OPTION_VAL_50_PT_PT},
         { "60",              OPTION_VAL_60_PT_PT},
         { "72",              OPTION_VAL_72_PT_PT},
         { "75",              OPTION_VAL_75_PT_PT},
         { "90",              OPTION_VAL_90_PT_PT},
         { "100",              OPTION_VAL_100_PT_PT},
         { "119",              OPTION_VAL_119_PT_PT},
         { "120",              OPTION_VAL_120_PT_PT},
         { "144",              OPTION_VAL_144_PT_PT},
         { "155",              OPTION_VAL_155_PT_PT},
         { "160",              OPTION_VAL_160_PT_PT},
         { "165",              OPTION_VAL_165_PT_PT},
         { "180",              OPTION_VAL_180_PT_PT},
         { "200",              OPTION_VAL_200_PT_PT},
         { "240",              OPTION_VAL_240_PT_PT},
         { "244",              OPTION_VAL_244_PT_PT},
         { "300",              OPTION_VAL_300_PT_PT},
         { "360",              OPTION_VAL_360_PT_PT},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_PT_PT,
      NULL,
      DOOM_RESOLUTION_INFO_0_PT_PT,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_PT_PT },
         { "640x368",   OPTION_VAL_640X368_PT_PT },
         { "720x408",   OPTION_VAL_720X408_PT_PT },
         { "960x544",   OPTION_VAL_960X544_PT_PT },
		 { "1280x720",   OPTION_VAL_1280X720_PT_PT },
		 { "1920x1080",   OPTION_VAL_1920X1080_PT_PT },
		 { "2560x1440",   OPTION_VAL_2560X1440_PT_PT },
		 { "3840x2160",   OPTION_VAL_3840X2160_PT_PT },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_PT_PT,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_PT_PT,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_PT_PT,
      NULL,
      DOOM_FPS_INFO_0_PT_PT,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_pt_pt = {
   option_cats_pt_pt,
   option_defs_pt_pt
};

/* RETRO_LANGUAGE_RO */

#define DOOM_FRAMERATE_LABEL_RO NULL
#define DOOM_FRAMERATE_INFO_0_RO NULL
#define OPTION_VAL_AUTO_RO NULL
#define OPTION_VAL_50_RO NULL
#define OPTION_VAL_60_RO NULL
#define OPTION_VAL_72_RO NULL
#define OPTION_VAL_75_RO NULL
#define OPTION_VAL_90_RO NULL
#define OPTION_VAL_100_RO NULL
#define OPTION_VAL_119_RO NULL
#define OPTION_VAL_120_RO NULL
#define OPTION_VAL_144_RO NULL
#define OPTION_VAL_155_RO NULL
#define OPTION_VAL_160_RO NULL
#define OPTION_VAL_165_RO NULL
#define OPTION_VAL_180_RO NULL
#define OPTION_VAL_200_RO NULL
#define OPTION_VAL_240_RO NULL
#define OPTION_VAL_244_RO NULL
#define OPTION_VAL_300_RO NULL
#define OPTION_VAL_360_RO NULL
#define DOOM_RESOLUTION_LABEL_RO NULL
#define DOOM_RESOLUTION_INFO_0_RO NULL
#define OPTION_VAL_480X272_RO NULL
#define OPTION_VAL_640X368_RO NULL
#define OPTION_VAL_720X408_RO NULL
#define OPTION_VAL_960X544_RO NULL
#define OPTION_VAL_1280X720_RO NULL
#define OPTION_VAL_1920X1080_RO NULL
#define OPTION_VAL_2560X1440_RO NULL
#define OPTION_VAL_3840X2160_RO NULL
#define DOOM_INVERT_Y_AXIS_LABEL_RO NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_RO NULL
#define DOOM_FPS_LABEL_RO NULL
#define DOOM_FPS_INFO_0_RO NULL

struct retro_core_option_v2_category option_cats_ro[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ro[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_RO,
      NULL,
      DOOM_FRAMERATE_INFO_0_RO,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_RO},
         { "50",              OPTION_VAL_50_RO},
         { "60",              OPTION_VAL_60_RO},
         { "72",              OPTION_VAL_72_RO},
         { "75",              OPTION_VAL_75_RO},
         { "90",              OPTION_VAL_90_RO},
         { "100",              OPTION_VAL_100_RO},
         { "119",              OPTION_VAL_119_RO},
         { "120",              OPTION_VAL_120_RO},
         { "144",              OPTION_VAL_144_RO},
         { "155",              OPTION_VAL_155_RO},
         { "160",              OPTION_VAL_160_RO},
         { "165",              OPTION_VAL_165_RO},
         { "180",              OPTION_VAL_180_RO},
         { "200",              OPTION_VAL_200_RO},
         { "240",              OPTION_VAL_240_RO},
         { "244",              OPTION_VAL_244_RO},
         { "300",              OPTION_VAL_300_RO},
         { "360",              OPTION_VAL_360_RO},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_RO,
      NULL,
      DOOM_RESOLUTION_INFO_0_RO,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_RO },
         { "640x368",   OPTION_VAL_640X368_RO },
         { "720x408",   OPTION_VAL_720X408_RO },
         { "960x544",   OPTION_VAL_960X544_RO },
		 { "1280x720",   OPTION_VAL_1280X720_RO },
		 { "1920x1080",   OPTION_VAL_1920X1080_RO },
		 { "2560x1440",   OPTION_VAL_2560X1440_RO },
		 { "3840x2160",   OPTION_VAL_3840X2160_RO },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_RO,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_RO,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_RO,
      NULL,
      DOOM_FPS_INFO_0_RO,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ro = {
   option_cats_ro,
   option_defs_ro
};

/* RETRO_LANGUAGE_RU */

#define DOOM_FRAMERATE_LABEL_RU "Частота кадров (требуется перезапуск)"
#define DOOM_FRAMERATE_INFO_0_RU "Выбор желаемой частоты кадров."
#define OPTION_VAL_AUTO_RU "Авто"
#define OPTION_VAL_50_RU "50 кадр/c"
#define OPTION_VAL_60_RU "60 кадр/c"
#define OPTION_VAL_72_RU "72 кадр/c"
#define OPTION_VAL_75_RU "75 кадр/c"
#define OPTION_VAL_90_RU "90 кадр/c"
#define OPTION_VAL_100_RU "100 кадр/c"
#define OPTION_VAL_119_RU "119 кадр/c"
#define OPTION_VAL_120_RU "120 кадр/c"
#define OPTION_VAL_144_RU "144 кадр/c"
#define OPTION_VAL_155_RU "155 кадр/c"
#define OPTION_VAL_160_RU "160 кадр/c"
#define OPTION_VAL_165_RU "165 кадр/c"
#define OPTION_VAL_180_RU "180 кадр/c"
#define OPTION_VAL_200_RU "200 кадр/c"
#define OPTION_VAL_240_RU "240 кадр/c"
#define OPTION_VAL_244_RU "244 кадр/c"
#define OPTION_VAL_300_RU "300 кадр/c"
#define OPTION_VAL_360_RU "360 кадр/c"
#define DOOM_RESOLUTION_LABEL_RU "Внутреннее разрешение (требуется перезапуск)"
#define DOOM_RESOLUTION_INFO_0_RU "Выбор разрешения рендеринга."
#define OPTION_VAL_480X272_RU NULL
#define OPTION_VAL_640X368_RU NULL
#define OPTION_VAL_720X408_RU NULL
#define OPTION_VAL_960X544_RU "960x544 (по умолчанию)"
#define OPTION_VAL_1280X720_RU NULL
#define OPTION_VAL_1920X1080_RU NULL
#define OPTION_VAL_2560X1440_RU NULL
#define OPTION_VAL_3840X2160_RU NULL
#define DOOM_INVERT_Y_AXIS_LABEL_RU "Инверсия оси Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_RU "Инвертировать правый аналоговый стик по оси Y."
#define DOOM_FPS_LABEL_RU "Показывать FPS"
#define DOOM_FPS_INFO_0_RU "Отображать на экране частоту кадров."

struct retro_core_option_v2_category option_cats_ru[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_ru[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_RU,
      NULL,
      DOOM_FRAMERATE_INFO_0_RU,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_RU},
         { "50",              OPTION_VAL_50_RU},
         { "60",              OPTION_VAL_60_RU},
         { "72",              OPTION_VAL_72_RU},
         { "75",              OPTION_VAL_75_RU},
         { "90",              OPTION_VAL_90_RU},
         { "100",              OPTION_VAL_100_RU},
         { "119",              OPTION_VAL_119_RU},
         { "120",              OPTION_VAL_120_RU},
         { "144",              OPTION_VAL_144_RU},
         { "155",              OPTION_VAL_155_RU},
         { "160",              OPTION_VAL_160_RU},
         { "165",              OPTION_VAL_165_RU},
         { "180",              OPTION_VAL_180_RU},
         { "200",              OPTION_VAL_200_RU},
         { "240",              OPTION_VAL_240_RU},
         { "244",              OPTION_VAL_244_RU},
         { "300",              OPTION_VAL_300_RU},
         { "360",              OPTION_VAL_360_RU},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_RU,
      NULL,
      DOOM_RESOLUTION_INFO_0_RU,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_RU },
         { "640x368",   OPTION_VAL_640X368_RU },
         { "720x408",   OPTION_VAL_720X408_RU },
         { "960x544",   OPTION_VAL_960X544_RU },
		 { "1280x720",   OPTION_VAL_1280X720_RU },
		 { "1920x1080",   OPTION_VAL_1920X1080_RU },
		 { "2560x1440",   OPTION_VAL_2560X1440_RU },
		 { "3840x2160",   OPTION_VAL_3840X2160_RU },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_RU,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_RU,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_RU,
      NULL,
      DOOM_FPS_INFO_0_RU,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_ru = {
   option_cats_ru,
   option_defs_ru
};

/* RETRO_LANGUAGE_SI */

#define DOOM_FRAMERATE_LABEL_SI NULL
#define DOOM_FRAMERATE_INFO_0_SI NULL
#define OPTION_VAL_AUTO_SI NULL
#define OPTION_VAL_50_SI NULL
#define OPTION_VAL_60_SI NULL
#define OPTION_VAL_72_SI NULL
#define OPTION_VAL_75_SI NULL
#define OPTION_VAL_90_SI NULL
#define OPTION_VAL_100_SI NULL
#define OPTION_VAL_119_SI NULL
#define OPTION_VAL_120_SI NULL
#define OPTION_VAL_144_SI NULL
#define OPTION_VAL_155_SI NULL
#define OPTION_VAL_160_SI NULL
#define OPTION_VAL_165_SI NULL
#define OPTION_VAL_180_SI NULL
#define OPTION_VAL_200_SI NULL
#define OPTION_VAL_240_SI NULL
#define OPTION_VAL_244_SI NULL
#define OPTION_VAL_300_SI NULL
#define OPTION_VAL_360_SI NULL
#define DOOM_RESOLUTION_LABEL_SI NULL
#define DOOM_RESOLUTION_INFO_0_SI NULL
#define OPTION_VAL_480X272_SI NULL
#define OPTION_VAL_640X368_SI NULL
#define OPTION_VAL_720X408_SI NULL
#define OPTION_VAL_960X544_SI NULL
#define OPTION_VAL_1280X720_SI NULL
#define OPTION_VAL_1920X1080_SI NULL
#define OPTION_VAL_2560X1440_SI NULL
#define OPTION_VAL_3840X2160_SI NULL
#define DOOM_INVERT_Y_AXIS_LABEL_SI NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_SI NULL
#define DOOM_FPS_LABEL_SI NULL
#define DOOM_FPS_INFO_0_SI NULL

struct retro_core_option_v2_category option_cats_si[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_si[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_SI,
      NULL,
      DOOM_FRAMERATE_INFO_0_SI,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_SI},
         { "50",              OPTION_VAL_50_SI},
         { "60",              OPTION_VAL_60_SI},
         { "72",              OPTION_VAL_72_SI},
         { "75",              OPTION_VAL_75_SI},
         { "90",              OPTION_VAL_90_SI},
         { "100",              OPTION_VAL_100_SI},
         { "119",              OPTION_VAL_119_SI},
         { "120",              OPTION_VAL_120_SI},
         { "144",              OPTION_VAL_144_SI},
         { "155",              OPTION_VAL_155_SI},
         { "160",              OPTION_VAL_160_SI},
         { "165",              OPTION_VAL_165_SI},
         { "180",              OPTION_VAL_180_SI},
         { "200",              OPTION_VAL_200_SI},
         { "240",              OPTION_VAL_240_SI},
         { "244",              OPTION_VAL_244_SI},
         { "300",              OPTION_VAL_300_SI},
         { "360",              OPTION_VAL_360_SI},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_SI,
      NULL,
      DOOM_RESOLUTION_INFO_0_SI,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_SI },
         { "640x368",   OPTION_VAL_640X368_SI },
         { "720x408",   OPTION_VAL_720X408_SI },
         { "960x544",   OPTION_VAL_960X544_SI },
		 { "1280x720",   OPTION_VAL_1280X720_SI },
		 { "1920x1080",   OPTION_VAL_1920X1080_SI },
		 { "2560x1440",   OPTION_VAL_2560X1440_SI },
		 { "3840x2160",   OPTION_VAL_3840X2160_SI },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_SI,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_SI,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_SI,
      NULL,
      DOOM_FPS_INFO_0_SI,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_si = {
   option_cats_si,
   option_defs_si
};

/* RETRO_LANGUAGE_SK */

#define DOOM_FRAMERATE_LABEL_SK NULL
#define DOOM_FRAMERATE_INFO_0_SK NULL
#define OPTION_VAL_AUTO_SK NULL
#define OPTION_VAL_50_SK NULL
#define OPTION_VAL_60_SK NULL
#define OPTION_VAL_72_SK NULL
#define OPTION_VAL_75_SK NULL
#define OPTION_VAL_90_SK NULL
#define OPTION_VAL_100_SK NULL
#define OPTION_VAL_119_SK NULL
#define OPTION_VAL_120_SK NULL
#define OPTION_VAL_144_SK NULL
#define OPTION_VAL_155_SK NULL
#define OPTION_VAL_160_SK NULL
#define OPTION_VAL_165_SK NULL
#define OPTION_VAL_180_SK NULL
#define OPTION_VAL_200_SK NULL
#define OPTION_VAL_240_SK NULL
#define OPTION_VAL_244_SK NULL
#define OPTION_VAL_300_SK NULL
#define OPTION_VAL_360_SK NULL
#define DOOM_RESOLUTION_LABEL_SK NULL
#define DOOM_RESOLUTION_INFO_0_SK NULL
#define OPTION_VAL_480X272_SK NULL
#define OPTION_VAL_640X368_SK NULL
#define OPTION_VAL_720X408_SK NULL
#define OPTION_VAL_960X544_SK NULL
#define OPTION_VAL_1280X720_SK NULL
#define OPTION_VAL_1920X1080_SK NULL
#define OPTION_VAL_2560X1440_SK NULL
#define OPTION_VAL_3840X2160_SK NULL
#define DOOM_INVERT_Y_AXIS_LABEL_SK NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_SK NULL
#define DOOM_FPS_LABEL_SK NULL
#define DOOM_FPS_INFO_0_SK NULL

struct retro_core_option_v2_category option_cats_sk[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_sk[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_SK,
      NULL,
      DOOM_FRAMERATE_INFO_0_SK,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_SK},
         { "50",              OPTION_VAL_50_SK},
         { "60",              OPTION_VAL_60_SK},
         { "72",              OPTION_VAL_72_SK},
         { "75",              OPTION_VAL_75_SK},
         { "90",              OPTION_VAL_90_SK},
         { "100",              OPTION_VAL_100_SK},
         { "119",              OPTION_VAL_119_SK},
         { "120",              OPTION_VAL_120_SK},
         { "144",              OPTION_VAL_144_SK},
         { "155",              OPTION_VAL_155_SK},
         { "160",              OPTION_VAL_160_SK},
         { "165",              OPTION_VAL_165_SK},
         { "180",              OPTION_VAL_180_SK},
         { "200",              OPTION_VAL_200_SK},
         { "240",              OPTION_VAL_240_SK},
         { "244",              OPTION_VAL_244_SK},
         { "300",              OPTION_VAL_300_SK},
         { "360",              OPTION_VAL_360_SK},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_SK,
      NULL,
      DOOM_RESOLUTION_INFO_0_SK,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_SK },
         { "640x368",   OPTION_VAL_640X368_SK },
         { "720x408",   OPTION_VAL_720X408_SK },
         { "960x544",   OPTION_VAL_960X544_SK },
		 { "1280x720",   OPTION_VAL_1280X720_SK },
		 { "1920x1080",   OPTION_VAL_1920X1080_SK },
		 { "2560x1440",   OPTION_VAL_2560X1440_SK },
		 { "3840x2160",   OPTION_VAL_3840X2160_SK },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_SK,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_SK,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_SK,
      NULL,
      DOOM_FPS_INFO_0_SK,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_sk = {
   option_cats_sk,
   option_defs_sk
};

/* RETRO_LANGUAGE_SR */

#define DOOM_FRAMERATE_LABEL_SR NULL
#define DOOM_FRAMERATE_INFO_0_SR NULL
#define OPTION_VAL_AUTO_SR NULL
#define OPTION_VAL_50_SR NULL
#define OPTION_VAL_60_SR NULL
#define OPTION_VAL_72_SR NULL
#define OPTION_VAL_75_SR NULL
#define OPTION_VAL_90_SR NULL
#define OPTION_VAL_100_SR NULL
#define OPTION_VAL_119_SR NULL
#define OPTION_VAL_120_SR NULL
#define OPTION_VAL_144_SR NULL
#define OPTION_VAL_155_SR NULL
#define OPTION_VAL_160_SR NULL
#define OPTION_VAL_165_SR NULL
#define OPTION_VAL_180_SR NULL
#define OPTION_VAL_200_SR NULL
#define OPTION_VAL_240_SR NULL
#define OPTION_VAL_244_SR NULL
#define OPTION_VAL_300_SR NULL
#define OPTION_VAL_360_SR NULL
#define DOOM_RESOLUTION_LABEL_SR NULL
#define DOOM_RESOLUTION_INFO_0_SR NULL
#define OPTION_VAL_480X272_SR NULL
#define OPTION_VAL_640X368_SR NULL
#define OPTION_VAL_720X408_SR NULL
#define OPTION_VAL_960X544_SR NULL
#define OPTION_VAL_1280X720_SR NULL
#define OPTION_VAL_1920X1080_SR NULL
#define OPTION_VAL_2560X1440_SR NULL
#define OPTION_VAL_3840X2160_SR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_SR NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_SR NULL
#define DOOM_FPS_LABEL_SR NULL
#define DOOM_FPS_INFO_0_SR NULL

struct retro_core_option_v2_category option_cats_sr[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_sr[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_SR,
      NULL,
      DOOM_FRAMERATE_INFO_0_SR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_SR},
         { "50",              OPTION_VAL_50_SR},
         { "60",              OPTION_VAL_60_SR},
         { "72",              OPTION_VAL_72_SR},
         { "75",              OPTION_VAL_75_SR},
         { "90",              OPTION_VAL_90_SR},
         { "100",              OPTION_VAL_100_SR},
         { "119",              OPTION_VAL_119_SR},
         { "120",              OPTION_VAL_120_SR},
         { "144",              OPTION_VAL_144_SR},
         { "155",              OPTION_VAL_155_SR},
         { "160",              OPTION_VAL_160_SR},
         { "165",              OPTION_VAL_165_SR},
         { "180",              OPTION_VAL_180_SR},
         { "200",              OPTION_VAL_200_SR},
         { "240",              OPTION_VAL_240_SR},
         { "244",              OPTION_VAL_244_SR},
         { "300",              OPTION_VAL_300_SR},
         { "360",              OPTION_VAL_360_SR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_SR,
      NULL,
      DOOM_RESOLUTION_INFO_0_SR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_SR },
         { "640x368",   OPTION_VAL_640X368_SR },
         { "720x408",   OPTION_VAL_720X408_SR },
         { "960x544",   OPTION_VAL_960X544_SR },
		 { "1280x720",   OPTION_VAL_1280X720_SR },
		 { "1920x1080",   OPTION_VAL_1920X1080_SR },
		 { "2560x1440",   OPTION_VAL_2560X1440_SR },
		 { "3840x2160",   OPTION_VAL_3840X2160_SR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_SR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_SR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_SR,
      NULL,
      DOOM_FPS_INFO_0_SR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_sr = {
   option_cats_sr,
   option_defs_sr
};

/* RETRO_LANGUAGE_SV */

#define DOOM_FRAMERATE_LABEL_SV NULL
#define DOOM_FRAMERATE_INFO_0_SV NULL
#define OPTION_VAL_AUTO_SV "Automatiskt"
#define OPTION_VAL_50_SV NULL
#define OPTION_VAL_60_SV NULL
#define OPTION_VAL_72_SV NULL
#define OPTION_VAL_75_SV NULL
#define OPTION_VAL_90_SV NULL
#define OPTION_VAL_100_SV NULL
#define OPTION_VAL_119_SV NULL
#define OPTION_VAL_120_SV NULL
#define OPTION_VAL_144_SV NULL
#define OPTION_VAL_155_SV NULL
#define OPTION_VAL_160_SV NULL
#define OPTION_VAL_165_SV NULL
#define OPTION_VAL_180_SV NULL
#define OPTION_VAL_200_SV NULL
#define OPTION_VAL_240_SV NULL
#define OPTION_VAL_244_SV NULL
#define OPTION_VAL_300_SV NULL
#define OPTION_VAL_360_SV NULL
#define DOOM_RESOLUTION_LABEL_SV NULL
#define DOOM_RESOLUTION_INFO_0_SV NULL
#define OPTION_VAL_480X272_SV NULL
#define OPTION_VAL_640X368_SV NULL
#define OPTION_VAL_720X408_SV NULL
#define OPTION_VAL_960X544_SV NULL
#define OPTION_VAL_1280X720_SV NULL
#define OPTION_VAL_1920X1080_SV NULL
#define OPTION_VAL_2560X1440_SV NULL
#define OPTION_VAL_3840X2160_SV NULL
#define DOOM_INVERT_Y_AXIS_LABEL_SV NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_SV NULL
#define DOOM_FPS_LABEL_SV "Visa FPS"
#define DOOM_FPS_INFO_0_SV NULL

struct retro_core_option_v2_category option_cats_sv[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_sv[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_SV,
      NULL,
      DOOM_FRAMERATE_INFO_0_SV,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_SV},
         { "50",              OPTION_VAL_50_SV},
         { "60",              OPTION_VAL_60_SV},
         { "72",              OPTION_VAL_72_SV},
         { "75",              OPTION_VAL_75_SV},
         { "90",              OPTION_VAL_90_SV},
         { "100",              OPTION_VAL_100_SV},
         { "119",              OPTION_VAL_119_SV},
         { "120",              OPTION_VAL_120_SV},
         { "144",              OPTION_VAL_144_SV},
         { "155",              OPTION_VAL_155_SV},
         { "160",              OPTION_VAL_160_SV},
         { "165",              OPTION_VAL_165_SV},
         { "180",              OPTION_VAL_180_SV},
         { "200",              OPTION_VAL_200_SV},
         { "240",              OPTION_VAL_240_SV},
         { "244",              OPTION_VAL_244_SV},
         { "300",              OPTION_VAL_300_SV},
         { "360",              OPTION_VAL_360_SV},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_SV,
      NULL,
      DOOM_RESOLUTION_INFO_0_SV,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_SV },
         { "640x368",   OPTION_VAL_640X368_SV },
         { "720x408",   OPTION_VAL_720X408_SV },
         { "960x544",   OPTION_VAL_960X544_SV },
		 { "1280x720",   OPTION_VAL_1280X720_SV },
		 { "1920x1080",   OPTION_VAL_1920X1080_SV },
		 { "2560x1440",   OPTION_VAL_2560X1440_SV },
		 { "3840x2160",   OPTION_VAL_3840X2160_SV },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_SV,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_SV,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_SV,
      NULL,
      DOOM_FPS_INFO_0_SV,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_sv = {
   option_cats_sv,
   option_defs_sv
};

/* RETRO_LANGUAGE_TR */

#define DOOM_FRAMERATE_LABEL_TR "Kare hızı (Yeniden Başlatılmalı)"
#define DOOM_FRAMERATE_INFO_0_TR "İstediğiniz kare hızını seçin."
#define OPTION_VAL_AUTO_TR "Otomatik"
#define OPTION_VAL_50_TR NULL
#define OPTION_VAL_60_TR NULL
#define OPTION_VAL_72_TR NULL
#define OPTION_VAL_75_TR NULL
#define OPTION_VAL_90_TR NULL
#define OPTION_VAL_100_TR NULL
#define OPTION_VAL_119_TR NULL
#define OPTION_VAL_120_TR NULL
#define OPTION_VAL_144_TR NULL
#define OPTION_VAL_155_TR NULL
#define OPTION_VAL_160_TR NULL
#define OPTION_VAL_165_TR NULL
#define OPTION_VAL_180_TR NULL
#define OPTION_VAL_200_TR NULL
#define OPTION_VAL_240_TR NULL
#define OPTION_VAL_244_TR NULL
#define OPTION_VAL_300_TR NULL
#define OPTION_VAL_360_TR NULL
#define DOOM_RESOLUTION_LABEL_TR "Dahili çözünürlük (Yeniden Başlatılmalı)"
#define DOOM_RESOLUTION_INFO_0_TR "Oluşturulacak çözünürlüğü seçin."
#define OPTION_VAL_480X272_TR NULL
#define OPTION_VAL_640X368_TR NULL
#define OPTION_VAL_720X408_TR NULL
#define OPTION_VAL_960X544_TR "960x544 (Varsayılan)"
#define OPTION_VAL_1280X720_TR NULL
#define OPTION_VAL_1920X1080_TR NULL
#define OPTION_VAL_2560X1440_TR NULL
#define OPTION_VAL_3840X2160_TR NULL
#define DOOM_INVERT_Y_AXIS_LABEL_TR "Y Eksenini Tersine Çevir"
#define DOOM_INVERT_Y_AXIS_INFO_0_TR "Sağ analog çubuğun Y eksenini ters çevirin."
#define DOOM_FPS_LABEL_TR "FPS Göster"
#define DOOM_FPS_INFO_0_TR "Ekranda kare hızını göster."

struct retro_core_option_v2_category option_cats_tr[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_tr[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_TR,
      NULL,
      DOOM_FRAMERATE_INFO_0_TR,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_TR},
         { "50",              OPTION_VAL_50_TR},
         { "60",              OPTION_VAL_60_TR},
         { "72",              OPTION_VAL_72_TR},
         { "75",              OPTION_VAL_75_TR},
         { "90",              OPTION_VAL_90_TR},
         { "100",              OPTION_VAL_100_TR},
         { "119",              OPTION_VAL_119_TR},
         { "120",              OPTION_VAL_120_TR},
         { "144",              OPTION_VAL_144_TR},
         { "155",              OPTION_VAL_155_TR},
         { "160",              OPTION_VAL_160_TR},
         { "165",              OPTION_VAL_165_TR},
         { "180",              OPTION_VAL_180_TR},
         { "200",              OPTION_VAL_200_TR},
         { "240",              OPTION_VAL_240_TR},
         { "244",              OPTION_VAL_244_TR},
         { "300",              OPTION_VAL_300_TR},
         { "360",              OPTION_VAL_360_TR},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_TR,
      NULL,
      DOOM_RESOLUTION_INFO_0_TR,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_TR },
         { "640x368",   OPTION_VAL_640X368_TR },
         { "720x408",   OPTION_VAL_720X408_TR },
         { "960x544",   OPTION_VAL_960X544_TR },
		 { "1280x720",   OPTION_VAL_1280X720_TR },
		 { "1920x1080",   OPTION_VAL_1920X1080_TR },
		 { "2560x1440",   OPTION_VAL_2560X1440_TR },
		 { "3840x2160",   OPTION_VAL_3840X2160_TR },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_TR,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_TR,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_TR,
      NULL,
      DOOM_FPS_INFO_0_TR,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_tr = {
   option_cats_tr,
   option_defs_tr
};

/* RETRO_LANGUAGE_UK */

#define DOOM_FRAMERATE_LABEL_UK "Частота кадрів (потрібен перезапуск)"
#define DOOM_FRAMERATE_INFO_0_UK "Виберіть бажану частоту кадрів."
#define OPTION_VAL_AUTO_UK "Авто"
#define OPTION_VAL_50_UK "50 кадрів"
#define OPTION_VAL_60_UK "60 кадрів"
#define OPTION_VAL_72_UK "72 кадри"
#define OPTION_VAL_75_UK "75 кадрів"
#define OPTION_VAL_90_UK "90 кадрів"
#define OPTION_VAL_100_UK "100 кадрів"
#define OPTION_VAL_119_UK "119 кадрів"
#define OPTION_VAL_120_UK "120 кадрів"
#define OPTION_VAL_144_UK "144 кадри"
#define OPTION_VAL_155_UK "155 кадрів"
#define OPTION_VAL_160_UK "160 кадрів"
#define OPTION_VAL_165_UK "165 кадрів"
#define OPTION_VAL_180_UK "180 кадрів"
#define OPTION_VAL_200_UK "200 кадрів"
#define OPTION_VAL_240_UK "240 кадрів"
#define OPTION_VAL_244_UK "244 кадри"
#define OPTION_VAL_300_UK "300 кадрів"
#define OPTION_VAL_360_UK "360 кадрів"
#define DOOM_RESOLUTION_LABEL_UK "Внутрішня роздільна здатність (потрібний перезапуск)"
#define DOOM_RESOLUTION_INFO_0_UK "Виберіть роздільну здатність для візуалізації."
#define OPTION_VAL_480X272_UK "480х272"
#define OPTION_VAL_640X368_UK "640х368"
#define OPTION_VAL_720X408_UK "720х408"
#define OPTION_VAL_960X544_UK "960x544 (за замовчуванням)"
#define OPTION_VAL_1280X720_UK NULL
#define OPTION_VAL_1920X1080_UK NULL
#define OPTION_VAL_2560X1440_UK "2560х1440"
#define OPTION_VAL_3840X2160_UK "3840х2160"
#define DOOM_INVERT_Y_AXIS_LABEL_UK "Інвертувати вісь Y"
#define DOOM_INVERT_Y_AXIS_INFO_0_UK "Інвертувати Y-вісь правого аналога."
#define DOOM_FPS_LABEL_UK "Показувати частоту кадрів"
#define DOOM_FPS_INFO_0_UK "Показувати частоту кадрів на екрані."

struct retro_core_option_v2_category option_cats_uk[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_uk[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_UK,
      NULL,
      DOOM_FRAMERATE_INFO_0_UK,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_UK},
         { "50",              OPTION_VAL_50_UK},
         { "60",              OPTION_VAL_60_UK},
         { "72",              OPTION_VAL_72_UK},
         { "75",              OPTION_VAL_75_UK},
         { "90",              OPTION_VAL_90_UK},
         { "100",              OPTION_VAL_100_UK},
         { "119",              OPTION_VAL_119_UK},
         { "120",              OPTION_VAL_120_UK},
         { "144",              OPTION_VAL_144_UK},
         { "155",              OPTION_VAL_155_UK},
         { "160",              OPTION_VAL_160_UK},
         { "165",              OPTION_VAL_165_UK},
         { "180",              OPTION_VAL_180_UK},
         { "200",              OPTION_VAL_200_UK},
         { "240",              OPTION_VAL_240_UK},
         { "244",              OPTION_VAL_244_UK},
         { "300",              OPTION_VAL_300_UK},
         { "360",              OPTION_VAL_360_UK},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_UK,
      NULL,
      DOOM_RESOLUTION_INFO_0_UK,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_UK },
         { "640x368",   OPTION_VAL_640X368_UK },
         { "720x408",   OPTION_VAL_720X408_UK },
         { "960x544",   OPTION_VAL_960X544_UK },
		 { "1280x720",   OPTION_VAL_1280X720_UK },
		 { "1920x1080",   OPTION_VAL_1920X1080_UK },
		 { "2560x1440",   OPTION_VAL_2560X1440_UK },
		 { "3840x2160",   OPTION_VAL_3840X2160_UK },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_UK,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_UK,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_UK,
      NULL,
      DOOM_FPS_INFO_0_UK,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_uk = {
   option_cats_uk,
   option_defs_uk
};

/* RETRO_LANGUAGE_VAL */

#define DOOM_FRAMERATE_LABEL_VAL NULL
#define DOOM_FRAMERATE_INFO_0_VAL "Escull els fotogrames per segon desitjats."
#define OPTION_VAL_AUTO_VAL "Selecció automàtica"
#define OPTION_VAL_50_VAL NULL
#define OPTION_VAL_60_VAL NULL
#define OPTION_VAL_72_VAL NULL
#define OPTION_VAL_75_VAL NULL
#define OPTION_VAL_90_VAL NULL
#define OPTION_VAL_100_VAL NULL
#define OPTION_VAL_119_VAL NULL
#define OPTION_VAL_120_VAL NULL
#define OPTION_VAL_144_VAL NULL
#define OPTION_VAL_155_VAL NULL
#define OPTION_VAL_160_VAL NULL
#define OPTION_VAL_165_VAL NULL
#define OPTION_VAL_180_VAL NULL
#define OPTION_VAL_200_VAL NULL
#define OPTION_VAL_240_VAL NULL
#define OPTION_VAL_244_VAL NULL
#define OPTION_VAL_300_VAL NULL
#define OPTION_VAL_360_VAL NULL
#define DOOM_RESOLUTION_LABEL_VAL NULL
#define DOOM_RESOLUTION_INFO_0_VAL "Escull la resolució que vols renderitzar."
#define OPTION_VAL_480X272_VAL "480×272"
#define OPTION_VAL_640X368_VAL "640×368"
#define OPTION_VAL_720X408_VAL "720×408"
#define OPTION_VAL_960X544_VAL "960x544 (predeterminada)"
#define OPTION_VAL_1280X720_VAL "1280×720"
#define OPTION_VAL_1920X1080_VAL "1920×1080"
#define OPTION_VAL_2560X1440_VAL "2560×1440"
#define OPTION_VAL_3840X2160_VAL "3840×2160"
#define DOOM_INVERT_Y_AXIS_LABEL_VAL NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_VAL NULL
#define DOOM_FPS_LABEL_VAL NULL
#define DOOM_FPS_INFO_0_VAL NULL

struct retro_core_option_v2_category option_cats_val[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_val[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_VAL,
      NULL,
      DOOM_FRAMERATE_INFO_0_VAL,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_VAL},
         { "50",              OPTION_VAL_50_VAL},
         { "60",              OPTION_VAL_60_VAL},
         { "72",              OPTION_VAL_72_VAL},
         { "75",              OPTION_VAL_75_VAL},
         { "90",              OPTION_VAL_90_VAL},
         { "100",              OPTION_VAL_100_VAL},
         { "119",              OPTION_VAL_119_VAL},
         { "120",              OPTION_VAL_120_VAL},
         { "144",              OPTION_VAL_144_VAL},
         { "155",              OPTION_VAL_155_VAL},
         { "160",              OPTION_VAL_160_VAL},
         { "165",              OPTION_VAL_165_VAL},
         { "180",              OPTION_VAL_180_VAL},
         { "200",              OPTION_VAL_200_VAL},
         { "240",              OPTION_VAL_240_VAL},
         { "244",              OPTION_VAL_244_VAL},
         { "300",              OPTION_VAL_300_VAL},
         { "360",              OPTION_VAL_360_VAL},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_VAL,
      NULL,
      DOOM_RESOLUTION_INFO_0_VAL,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_VAL },
         { "640x368",   OPTION_VAL_640X368_VAL },
         { "720x408",   OPTION_VAL_720X408_VAL },
         { "960x544",   OPTION_VAL_960X544_VAL },
		 { "1280x720",   OPTION_VAL_1280X720_VAL },
		 { "1920x1080",   OPTION_VAL_1920X1080_VAL },
		 { "2560x1440",   OPTION_VAL_2560X1440_VAL },
		 { "3840x2160",   OPTION_VAL_3840X2160_VAL },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_VAL,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_VAL,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_VAL,
      NULL,
      DOOM_FPS_INFO_0_VAL,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_val = {
   option_cats_val,
   option_defs_val
};

/* RETRO_LANGUAGE_VN */

#define DOOM_FRAMERATE_LABEL_VN NULL
#define DOOM_FRAMERATE_INFO_0_VN NULL
#define OPTION_VAL_AUTO_VN "Tự động"
#define OPTION_VAL_50_VN NULL
#define OPTION_VAL_60_VN NULL
#define OPTION_VAL_72_VN NULL
#define OPTION_VAL_75_VN NULL
#define OPTION_VAL_90_VN NULL
#define OPTION_VAL_100_VN NULL
#define OPTION_VAL_119_VN NULL
#define OPTION_VAL_120_VN NULL
#define OPTION_VAL_144_VN NULL
#define OPTION_VAL_155_VN NULL
#define OPTION_VAL_160_VN NULL
#define OPTION_VAL_165_VN NULL
#define OPTION_VAL_180_VN NULL
#define OPTION_VAL_200_VN NULL
#define OPTION_VAL_240_VN NULL
#define OPTION_VAL_244_VN NULL
#define OPTION_VAL_300_VN NULL
#define OPTION_VAL_360_VN NULL
#define DOOM_RESOLUTION_LABEL_VN NULL
#define DOOM_RESOLUTION_INFO_0_VN NULL
#define OPTION_VAL_480X272_VN NULL
#define OPTION_VAL_640X368_VN NULL
#define OPTION_VAL_720X408_VN NULL
#define OPTION_VAL_960X544_VN NULL
#define OPTION_VAL_1280X720_VN NULL
#define OPTION_VAL_1920X1080_VN NULL
#define OPTION_VAL_2560X1440_VN NULL
#define OPTION_VAL_3840X2160_VN NULL
#define DOOM_INVERT_Y_AXIS_LABEL_VN NULL
#define DOOM_INVERT_Y_AXIS_INFO_0_VN NULL
#define DOOM_FPS_LABEL_VN NULL
#define DOOM_FPS_INFO_0_VN NULL

struct retro_core_option_v2_category option_cats_vn[] = {
   { NULL, NULL, NULL },
};
struct retro_core_option_v2_definition option_defs_vn[] = {
	{
      "doom_framerate",
      DOOM_FRAMERATE_LABEL_VN,
      NULL,
      DOOM_FRAMERATE_INFO_0_VN,
      NULL,
      NULL,
      {
         { "auto",            OPTION_VAL_AUTO_VN},
         { "50",              OPTION_VAL_50_VN},
         { "60",              OPTION_VAL_60_VN},
         { "72",              OPTION_VAL_72_VN},
         { "75",              OPTION_VAL_75_VN},
         { "90",              OPTION_VAL_90_VN},
         { "100",              OPTION_VAL_100_VN},
         { "119",              OPTION_VAL_119_VN},
         { "120",              OPTION_VAL_120_VN},
         { "144",              OPTION_VAL_144_VN},
         { "155",              OPTION_VAL_155_VN},
         { "160",              OPTION_VAL_160_VN},
         { "165",              OPTION_VAL_165_VN},
         { "180",              OPTION_VAL_180_VN},
         { "200",              OPTION_VAL_200_VN},
         { "240",              OPTION_VAL_240_VN},
         { "244",              OPTION_VAL_244_VN},
         { "300",              OPTION_VAL_300_VN},
         { "360",              OPTION_VAL_360_VN},
         { NULL, NULL },
      },
      "auto"
   },
   {
      "doom_resolution",
      DOOM_RESOLUTION_LABEL_VN,
      NULL,
      DOOM_RESOLUTION_INFO_0_VN,
      NULL,
      NULL,
      {
         { "480x272",   OPTION_VAL_480X272_VN },
         { "640x368",   OPTION_VAL_640X368_VN },
         { "720x408",   OPTION_VAL_720X408_VN },
         { "960x544",   OPTION_VAL_960X544_VN },
		 { "1280x720",   OPTION_VAL_1280X720_VN },
		 { "1920x1080",   OPTION_VAL_1920X1080_VN },
		 { "2560x1440",   OPTION_VAL_2560X1440_VN },
		 { "3840x2160",   OPTION_VAL_3840X2160_VN },
         { NULL, NULL },
      },
      "960x544"
   },
   {
      "doom_invert_y_axis",
      DOOM_INVERT_Y_AXIS_LABEL_VN,
      NULL,
      DOOM_INVERT_Y_AXIS_INFO_0_VN,
      NULL,
      NULL,
      {
         { "enabled",   "Enabled" },
         { "disabled",  "Disabled" },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "doom_fps",
      DOOM_FPS_LABEL_VN,
      NULL,
      DOOM_FPS_INFO_0_VN,
      NULL,
      NULL,
      {
         { "disabled",  "Disabled" },
         { "enabled",   "Enabled" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_vn = {
   option_cats_vn,
   option_defs_vn
};


#ifdef __cplusplus
}
#endif

#endif