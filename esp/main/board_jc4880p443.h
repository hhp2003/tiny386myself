// requires esp-idf v6.0.x
#define BUILD_ESP32 //用于条件编译，标记目标平台为 ESP32 系列（启用与 ESP32 相关的代码分支）

#define PSRAM_ALLOC_LEN (10 * 1024 * 1024)//指定从 PSRAM 预留/分配的大小（10 MiB），常用于帧缓冲、资源缓存等大内存分配。

// XXX: ld reports "error: Total discarded sections size is X bytes"
//#define IRAM_ATTR_CPU_EXEC1 IRAM_ATTR
#define IRAM_ATTR_CPU_EXEC1 //当前定义为空，表示项目中可用该宏标记函数，但不强制放入 IRAM；方便在需要时切换到 IRAM。

#define BPP 16 //每个像素点的bit数 = 16（通常 RGB565）；影响帧缓冲大小和屏幕像素格式
#define FULL_UPDATE //启用“全屏更新”刷新策略（而非局部增量刷新）。
#define SWAPXY //交换屏幕 X/Y 坐标映射（用于屏幕方向/排线反转）。
#define USE_LCD_ST7701 //启用 ST7701 LCD 驱动相关代码（在代码里用 #ifdef USE_LCD_ST7701 见 lcd_st7701.c）
#define LCD_WIDTH 800 //（宽 800、高 480）
#define LCD_HEIGHT 480

//定义 SDMMC/SDIO 信号对应的 GPIO 编号，用于初始化 SD 卡接口。
//SD卡接口在这里配置，sdkconfig里面没有SD卡接口配置
#define SD_CLK 43
#define SD_CMD 44
#define SD_D0 39
#define SD_D1 40
#define SD_D2 41
#define SD_D3 42
#define SD_PWR_CTRL_LDO_IO_ID 4 //用于控制 SD 卡供电（LDO 或 电源开关）的 GPIO，引脚用于上电/断电控制。
//sdkconfig.jc4880p443 与 esp_hosted 配置里也有 SDIO 引脚/slot 配置（hosted 模式），需注意一致性。
//但是与sdkconfig里的SDIO_PIN标号不一致，这里应该是SD card处的GPIO，不是hosted的SDIO

#define USE_HOSTED_WIFI //启用“hosted wifi”模式

//iis是跟音频有关的？我们不使用音频
#define MIXER_BUF_LEN 512 //音频混音缓冲区长度（样本或字节，取决于实现）；影响音频缓冲与延时。
#define I2S_MCLK 13 //定义 I2S 外设使用的 GPIO
#define I2S_BCLK 12
#define I2S_WS   10
#define I2S_DOUT 9
#define I2S_NUM  0

//也是跟音频相关的
#define USE_ES8311 //启用 ES8311 音频 codec 驱动（sdkconfig 中也有 CODEC_ES8311_SUPPORT）。
#define ES8311_PA 11
#define ES8311_I2C_NUM 0
#define ES8311_I2C_SDA 7
#define ES8311_I2C_SCL 8
