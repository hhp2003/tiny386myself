#ifdef USE_LCD_EK79007

#include "common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h" //这个依赖esp_lcd库
#include "sdkconfig.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"

#include "esp_lcd_ek79007.h"

#include "bsp_err_check.h"

#include "driver/gpio.h"
#include "driver/ledc.h"


#ifndef CONFIG_BSP_LCD_DPI_BUFFER_NUMS
#define CONFIG_BSP_LCD_DPI_BUFFER_NUMS  (2)
#endif


//设置接口
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_20)
#define BSP_LCD_RST           (GPIO_NUM_NC)
// #define BSP_LCD_TOUCH_RST     (GPIO_NUM_23)
// #define BSP_LCD_TOUCH_INT     (GPIO_NUM_21)

#define BSP_LCD_PIXEL_CLOCK_MHZ     (80)

static const char *TAG = "MY_LCD_EK79007_DRIVER";


#define ESP_LCD_COLOR_FORMAT_RGB565    (1)

#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB565)

/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)
/* LCD display color space */
#define BSP_LCD_COLOR_SPACE         (LCD_RGB_ELEMENT_ORDER_RGB)

#define BSP_LCD_H_RES              (1024)
#define BSP_LCD_V_RES              (600)

#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000) // 1Gbps

#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#ifndef CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH
#define CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH  (1)
#endif
#define LCD_LEDC_CH CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP HDMI resolution types
 *
 */
typedef enum {
    BSP_HDMI_RES_NONE = 0,
    BSP_HDMI_RES_800x600,   /*!< 800x600@60HZ   */
    BSP_HDMI_RES_1024x768,  /*!< 1024x768@60HZ  */
    BSP_HDMI_RES_1280x720,  /*!< 1280x720@60HZ  */
    BSP_HDMI_RES_1280x800,  /*!< 1280x800@60HZ  */
    BSP_HDMI_RES_1920x1080  /*!< 1920x1080@30HZ */
} bsp_hdmi_resolution_t;

/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    bsp_hdmi_resolution_t hdmi_resolution;    /*!< HDMI resolution selection */
    struct {
        mipi_dsi_phy_clock_source_t phy_clk_src;  /*!< DSI bus config - clock source */
        uint32_t lane_bit_rate_mbps;              /*!< DSI bus config - lane bit rate */
    } dsi_bus;
} bsp_display_config_t;

/**
 * @brief BSP display return handles
 *
 */
typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;  /*!< MIPI DSI bus handle */
    esp_lcd_panel_io_handle_t   io;            /*!< ESP LCD IO handle */
    esp_lcd_panel_handle_t      panel;         /*!< ESP LCD panel (color) handle */
    esp_lcd_panel_handle_t      control;       /*!< ESP LCD panel (control) handle */
} bsp_lcd_handles_t;

static bsp_lcd_handles_t disp_handles;
static esp_ldo_channel_handle_t disp_phy_pwr_chan = NULL;
static esp_lcd_panel_io_handle_t tp_io_handle = NULL;

//--------------------以下是函数声明------------------

/**
 * @brief Create new display panel
 *
 * For maximum flexibility, this function performs only reset and initialization of the display.
 * You must turn on the display explicitly by calling esp_lcd_panel_disp_on_off().
 * The display's backlight is not turned on either. You can use bsp_display_backlight_on/off(),
 * bsp_display_brightness_set() (on supported boards) or implement your own backlight control.
 *
 * If you want to free resources allocated by this function, you can use esp_lcd API, ie.:
 *
 * \code{.c}
 * esp_lcd_panel_del(panel);
 * esp_lcd_panel_io_del(io);
 * esp_lcd_del_dsi_bus(mipi_dsi_bus);
 * \endcode
 *
 * @param[in]  config    display configuration
 * @param[out] ret_panel esp_lcd panel handle
 * @param[out] ret_io    esp_lcd IO handle
 * @return
 *      - ESP_OK         On success
 *      - Else           esp_lcd failure
 */
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief Create new display panel
 *
 * For maximum flexibility, this function performs only reset and initialization of the display.
 * You must turn on the display explicitly by calling esp_lcd_panel_disp_on_off().
 * The display's backlight is not turned on either. You can use bsp_display_backlight_on/off(),
 * bsp_display_brightness_set() (on supported boards) or implement your own backlight control.
 *
 * If you want to free resources allocated by this function, you can use API:
 *
 * \code{.c}
 * bsp_display_delete();
 * \endcode
 *
 * @param[in]  config    display configuration
 * @param[out] ret_handles all esp_lcd handles in one structure
 * @return
 *      - ESP_OK         On success
 *      - Else           esp_lcd failure
 */
esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles);

/**
 * @brief Delete display panel
 */
void bsp_display_delete(void);

/**
 * @brief Initialize display's brightness
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_brightness_init(void);

/**
 * @brief Deinitialize display's brightness
 */
esp_err_t bsp_display_brightness_deinit(void);

/**
 * @brief Set display's brightness
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @param[in] brightness_percent Brightness in [%]
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);

/**
 * @brief Turn on display backlight
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_backlight_on(void);

/**
 * @brief Turn off display backlight
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif

//----------上面是声明，下面是定义--------------
static esp_err_t bsp_enable_dsi_phy_power(void)
{
#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &disp_phy_pwr_chan), TAG, "Acquire LDO channel for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

    return ESP_OK;
}
//-------------------------------------------------------------

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    bsp_lcd_handles_t handles;
    ret = bsp_display_new_with_handles(config, &handles);

    *ret_panel = handles.panel;
    *ret_io = handles.io;

    return ret;
}
//-------------------------------------------------------------

// esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles)
// {
//     esp_err_t ret = ESP_OK;
//     esp_lcd_panel_io_handle_t io = NULL;
//     esp_lcd_panel_handle_t disp_panel = NULL;

//     ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
//     ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY power failed");

//     /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
//     esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
//     esp_lcd_dsi_bus_config_t bus_config = {
//         .bus_id = 0,
//         .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
//         .phy_clk_src = config->dsi_bus.phy_clk_src,
//         .lane_bit_rate_mbps = config->dsi_bus.lane_bit_rate_mbps,
//     };
//     ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "New DSI bus init failed");
//     	// Give PHY a moment to stabilize before DBI transfers
// 	vTaskDelay(pdMS_TO_TICKS(50));

// #if !CONFIG_BSP_LCD_TYPE_HDMI
//     if (config->hdmi_resolution != BSP_HDMI_RES_NONE) {
//         ESP_LOGW(TAG, "Please select HDMI in menuconfig, if you want to use it.");
//     }

//     ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
//     // we use DBI interface to send LCD commands and parameters
//     esp_lcd_dbi_io_config_t dbi_config = {
//         .virtual_channel = 0,
//         .lcd_cmd_bits = 8,   // according to the LCD spec
//         .lcd_param_bits = 8, // according to the LCD spec
//     };
//     ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), err, TAG, "New panel IO failed");
// #endif

// #if CONFIG_BSP_LCD_TYPE_1024_600
//     // create EK79007 control panel
//     ESP_LOGI(TAG, "Install EK79007 LCD control panel");

// #if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
//     esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB888);
// #else
//     esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB565);
// #endif
//     dpi_config.num_fbs = CONFIG_BSP_LCD_DPI_BUFFER_NUMS;

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))
//     dpi_config.flags.use_dma2d = true;
// #endif

//     ek79007_vendor_config_t vendor_config = {
//         .mipi_config = {
//             .dsi_bus = mipi_dsi_bus,
//             .dpi_config = &dpi_config,
//         },
//     };
//     esp_lcd_panel_dev_config_t lcd_dev_config = {
//         .bits_per_pixel = 16,
//         .rgb_ele_order = BSP_LCD_COLOR_SPACE,
//         .reset_gpio_num = BSP_LCD_RST,
//         .vendor_config = &vendor_config,
//     };
//     ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ek79007(io, &lcd_dev_config, &disp_panel), err, TAG,
//                       "New LCD panel EK79007 failed");

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
//     ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(disp_panel), err, TAG, "LCD panel enable DMA2D failed");
// #endif

//     ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
//     ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");
// #elif CONFIG_BSP_LCD_TYPE_1280_800
//     // create ILI9881C control panel
//     ESP_LOGI(TAG, "Install ILI9881C LCD control panel");
// #if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
//     esp_lcd_dpi_panel_config_t dpi_config = ILI9881C_800_1280_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB888);
// #else
//     esp_lcd_dpi_panel_config_t dpi_config = ILI9881C_800_1280_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
// #endif
//     dpi_config.num_fbs = CONFIG_BSP_LCD_DPI_BUFFER_NUMS;

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))
//     dpi_config.flags.use_dma2d = true;
// #endif

//     ili9881c_vendor_config_t vendor_config = {
//         .mipi_config = {
//             .dsi_bus = mipi_dsi_bus,
//             .dpi_config = &dpi_config,
//             .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
//         },
//     };
//     const esp_lcd_panel_dev_config_t lcd_dev_config = {
//         .reset_gpio_num = BSP_LCD_RST,
//         .rgb_ele_order = BSP_LCD_COLOR_SPACE,
//         .bits_per_pixel = 16,
//         .vendor_config = &vendor_config,
//     };
//     ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ili9881c(io, &lcd_dev_config, &disp_panel), err, TAG,
//                       "New LCD panel ILI9881C failed");

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
//     ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(disp_panel), err, TAG, "LCD panel enable DMA2D failed");
// #endif

//     ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
//     ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");
//     ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(disp_panel, true), err, TAG, "LCD panel ON failed");

// #elif CONFIG_BSP_LCD_TYPE_HDMI

// #if !CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
// #error The color format must be RGB888 in HDMI display type!
// #endif
//     ESP_LOGI(TAG, "Install MIPI DSI HDMI control panel");
//     ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");

//     /* Main IO */
//     esp_lcd_panel_io_i2c_config_t io_config = LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_MAIN_ADDRESS);
//     ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config, &io));

//     /* CEC DSI IO */
//     esp_lcd_panel_io_handle_t io_cec_dsi = NULL;
//     esp_lcd_panel_io_i2c_config_t io_config_cec = LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_CEC_ADDRESS);
//     ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config_cec, &io_cec_dsi));

//     /* AVI IO */
//     esp_lcd_panel_io_handle_t io_avi = NULL;
//     esp_lcd_panel_io_i2c_config_t io_config_avi = LT8912B_IO_CFG(CONFIG_BSP_I2C_CLK_SPEED_HZ, LT8912B_IO_I2C_AVI_ADDRESS);
//     ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &io_config_avi, &io_avi));

//     esp_lcd_dpi_panel_config_t dpi_configs[] = {
//         LT8912B_800x600_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
//         LT8912B_1024x768_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
//         LT8912B_1280x720_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
//         LT8912B_1280x800_PANEL_60HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS),
//         LT8912B_1920x1080_PANEL_30HZ_DPI_CONFIG_WITH_FBS(CONFIG_BSP_LCD_DPI_BUFFER_NUMS)
//     };

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))
//     for (int i = 0; i < sizeof(dpi_configs) / sizeof(dpi_configs[0]); i++) {
//         dpi_configs[i].flags.use_dma2d = true;
//     }
// #endif

//     const esp_lcd_panel_lt8912b_video_timing_t video_timings[] = {
//         ESP_LCD_LT8912B_VIDEO_TIMING_800x600_60Hz(),
//         ESP_LCD_LT8912B_VIDEO_TIMING_1024x768_60Hz(),
//         ESP_LCD_LT8912B_VIDEO_TIMING_1280x720_60Hz(),
//         ESP_LCD_LT8912B_VIDEO_TIMING_1280x800_60Hz(),
//         ESP_LCD_LT8912B_VIDEO_TIMING_1920x1080_30Hz()
//     };
//     lt8912b_vendor_config_t vendor_config = {
//         .mipi_config = {
//             .dsi_bus = mipi_dsi_bus,
//             .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
//         },
//     };

//     /* DPI config */
//     switch (config->hdmi_resolution) {
//     case BSP_HDMI_RES_800x600:
//         ESP_LOGI(TAG, "HDMI configuration for 800x600@60HZ");
//         vendor_config.mipi_config.dpi_config = &dpi_configs[0];
//         memcpy(&vendor_config.video_timing, &video_timings[0], sizeof(esp_lcd_panel_lt8912b_video_timing_t));
//         break;
//     case BSP_HDMI_RES_1024x768:
//         ESP_LOGI(TAG, "HDMI configuration for 1024x768@60HZ");
//         vendor_config.mipi_config.dpi_config = &dpi_configs[1];
//         memcpy(&vendor_config.video_timing, &video_timings[1], sizeof(esp_lcd_panel_lt8912b_video_timing_t));
//         break;
//     case BSP_HDMI_RES_1280x720:
//         ESP_LOGI(TAG, "HDMI configuration for 1280x720@60HZ");
//         vendor_config.mipi_config.dpi_config = &dpi_configs[2];
//         memcpy(&vendor_config.video_timing, &video_timings[2], sizeof(esp_lcd_panel_lt8912b_video_timing_t));
//         break;
//     case BSP_HDMI_RES_1280x800:
//         ESP_LOGI(TAG, "HDMI configuration for 1280x800@60HZ");
//         vendor_config.mipi_config.dpi_config = &dpi_configs[3];
//         memcpy(&vendor_config.video_timing, &video_timings[3], sizeof(esp_lcd_panel_lt8912b_video_timing_t));
//         break;
//     case BSP_HDMI_RES_1920x1080:
//         ESP_LOGI(TAG, "HDMI configuration for 1920x1080@30HZ");
//         vendor_config.mipi_config.dpi_config = &dpi_configs[4];
//         memcpy(&vendor_config.video_timing, &video_timings[4], sizeof(esp_lcd_panel_lt8912b_video_timing_t));
//         break;
//     default:
//         ESP_LOGE(TAG, "Unsupported display type (%d)", config->hdmi_resolution);
//     }

//     const esp_lcd_panel_dev_config_t panel_config = {
//         .bits_per_pixel = 24,
//         .rgb_ele_order = BSP_LCD_COLOR_SPACE,
//         .reset_gpio_num = BSP_LCD_RST,
//         .vendor_config = &vendor_config,
//     };
//     const esp_lcd_panel_lt8912b_io_t io_all = {
//         .main = io,
//         .cec_dsi = io_cec_dsi,
//         .avi = io_avi,
//     };
//     ESP_ERROR_CHECK(esp_lcd_new_panel_lt8912b(&io_all, &panel_config, &disp_panel));

// #if CONFIG_BSP_LCD_USE_DMA2D && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
//     ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(disp_panel), err, TAG, "LCD panel enable DMA2D failed");
// #endif

//     ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
//     ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");

// #endif //CONFIG_BSP_LCD_TYPE_

//     /* Return all handles */
//     ret_handles->io = io;
//     disp_handles.io = io;
// #if CONFIG_BSP_LCD_TYPE_HDMI
//     ret_handles->io_cec = io_cec_dsi;
//     disp_handles.io_cec = io_cec_dsi;
//     ret_handles->io_avi = io_avi;
//     disp_handles.io_avi = io_avi;
// #endif
//     ret_handles->mipi_dsi_bus = mipi_dsi_bus;
//     disp_handles.mipi_dsi_bus = mipi_dsi_bus;
//     ret_handles->panel = disp_panel;
//     disp_handles.panel = disp_panel;
//     ret_handles->control = NULL;
//     disp_handles.control = NULL;

//     ESP_LOGI(TAG, "Display initialized");

//     return ret;

// err:
//     bsp_display_delete();
//     return ret;
// }

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t disp_panel = NULL;

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
    ESP_RETURN_ON_ERROR(bsp_enable_dsi_phy_power(), TAG, "DSI PHY power failed");

    /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = config->dsi_bus.phy_clk_src,
        .lane_bit_rate_mbps = config->dsi_bus.lane_bit_rate_mbps,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "New DSI bus init failed");
	// Give PHY a moment to stabilize before DBI transfers
	vTaskDelay(pdMS_TO_TICKS(50));

    if (config->hdmi_resolution != BSP_HDMI_RES_NONE) {
        ESP_LOGW(TAG, "Please select HDMI in menuconfig, if you want to use it.");
    }

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD spec
        .lcd_param_bits = 8, // according to the LCD spec
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), err, TAG, "New panel IO failed");

    // create EK79007 control panel
    ESP_LOGI(TAG, "Install EK79007 LCD control panel");

    esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB565);//这里不同的espidf版本需要调用的函数是不同的，以及输入参数的宏名称都不同
    //EK79007_1024_600_PANEL_60HZ_CONFIG_CF是ek79007组件里的宏函数
    //LCD_COLOR_FMT_RGB565宏的定义在E:\esp32idf\.espressif\v6.0.1\esp-idf\components\esp_hal_lcd\include\hal\lcd_types.h

    dpi_config.num_fbs = CONFIG_BSP_LCD_DPI_BUFFER_NUMS;//CONFIG_BSP_LCD_DPI_BUFFER_NUMS会在sdkconfig.h里定义，一般是2

    #if (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))
        dpi_config.flags.use_dma2d = true;
    #endif

    ek79007_vendor_config_t vendor_config = {
        .mipi_config =
            {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .reset_gpio_num = BSP_LCD_RST,
        .flags.reset_active_high = 1,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ek79007(io, &lcd_dev_config, &disp_panel), err, TAG, "New LCD panel EK79007 failed");

    #if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
    ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(disp_panel), err, TAG, "LCD panel enable DMA2D failed");
    #endif

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");

    /* Return all handles */
    ret_handles->io = io;
    disp_handles.io = io;
    ret_handles->mipi_dsi_bus = mipi_dsi_bus;
    disp_handles.mipi_dsi_bus = mipi_dsi_bus;
    ret_handles->panel = disp_panel;
    disp_handles.panel = disp_panel;
    ret_handles->control = NULL;
    disp_handles.control = NULL;

    ESP_LOGI(TAG, "Display initialized(finished)");

    return ret;

err:
    bsp_display_delete();
    return ret;
}

//-------------------------------------------------------------
void bsp_display_delete(void)
{
    if (disp_handles.panel) {
        esp_lcd_panel_del(disp_handles.panel);
        disp_handles.panel = NULL;
    }
    if (disp_handles.io) {
        esp_lcd_panel_io_del(disp_handles.io);
        disp_handles.io = NULL;
    }
#if CONFIG_BSP_LCD_TYPE_HDMI
    if (disp_handles.io_cec) {
        esp_lcd_panel_io_del(disp_handles.io_cec);
        disp_handles.io_cec = NULL;
    }
    if (disp_handles.io_avi) {
        esp_lcd_panel_io_del(disp_handles.io_avi);
        disp_handles.io_avi = NULL;
    }
#endif
    if (disp_handles.mipi_dsi_bus) {
        esp_lcd_del_dsi_bus(disp_handles.mipi_dsi_bus);
        disp_handles.mipi_dsi_bus = NULL;
    }

    if (disp_phy_pwr_chan) {
        esp_ldo_release_channel(disp_phy_pwr_chan);
        disp_phy_pwr_chan = NULL;
    }

    bsp_display_brightness_deinit();
}
//-------------------------------------------------------------
esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&LCD_backlight_timer));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_channel_config(&LCD_backlight_channel));
    return ESP_OK;
}
//-------------------------------------------------------------
esp_err_t bsp_display_brightness_deinit(void)
{
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = 1,
        .deconfigure = 1
    };
    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_pause(LEDC_LOW_SPEED_MODE, 1));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&LCD_backlight_timer));
    return ESP_OK;
}
//-------------------------------------------------------------



esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100; // LEDC resolution set to 10bits, thus: 100% = 1023
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
    return ESP_OK;
}
//-------------------------------------------------------------
esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}
//-------------------------------------------------------------
esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}


//-------下面调用上面的函数

uint16_t show[200*200] ;

//初始化lcd
void my_lcd_ek79007_init(void)
{

    //还是不要随意加ESP_ERROR_CHECK这些，它导致执行了硬件层面有问题的函数，让MCU停滞

    bsp_display_config_t config=   {
                .hdmi_resolution = BSP_HDMI_RES_NONE,
                .dsi_bus =
                    {
                        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                        .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
                    },
            };

    bsp_lcd_handles_t lcd_handles;
    esp_lcd_panel_handle_t lcd_panel; 

	ESP_LOGI(TAG, "the start of Init lcd ek79007");

    //先初始化背光
    //BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());
    bsp_display_brightness_init();

    //创建新句柄
    //BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new_with_handles(&config, &lcd_handles));
    bsp_display_new_with_handles(&config, &lcd_handles);


    lcd_panel=lcd_handles.panel; 

    //用于控制显示是否需要水平，垂直翻转
    // ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel, true, false));
    esp_lcd_panel_mirror(lcd_panel, true, false);

    //确保显示打开，true为开，false为关
	// ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));
    esp_lcd_panel_disp_on_off(lcd_panel, true);//但是报没法使用的错误，但是不影响，应该可以删掉这个函数

    //------ek79007初始化完了

	bsp_display_brightness_init();
	bsp_display_brightness_set(30);

	globals.panel = lcd_panel;

}


void pc_vga_step(void *o);

void lcd_draw(int x_start, int y_start, int x_end, int y_end, void *src)
{
	if (globals.panel) {
		ESP_ERROR_CHECK(
			esp_lcd_panel_draw_bitmap(
				globals.panel,
				x_start, y_start,
				x_end, y_end,
				src));
	}
}

void vga_task(void *arg)
{
	int core_id = esp_cpu_get_core_id();
	fprintf(stderr, "vga runs on core %d\n", core_id);

	ESP_LOGI(TAG, "Init display");

	my_lcd_ek79007_init();


	/* Signal i386_task that the LCD panel is ready */
	xEventGroupSetBits(global_event_group, BIT1);
	xEventGroupWaitBits(global_event_group,
			    BIT0,
			    pdFALSE,
			    pdFALSE,
			    portMAX_DELAY);

	while (1) {
		pc_vga_step(globals.pc);
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}
#endif
