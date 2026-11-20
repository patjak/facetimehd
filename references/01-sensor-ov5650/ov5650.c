/*
 * A V4L2 driver for OV ov5650 cameras.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-mediabus.h>
#include <linux/io.h>


#include "camera.h"


MODULE_AUTHOR("raymonxiu");
MODULE_DESCRIPTION("A low-level driver for OV5650 sensors");
MODULE_LICENSE("GPL");

//for internel driver debug
#define DEV_DBG_EN      0 
#if(DEV_DBG_EN == 1)    
#define vfe_dev_dbg(x,arg...) printk("[OV5650]"x,##arg)
#else
#define vfe_dev_dbg(x,arg...) 
#endif
#define vfe_dev_err(x,arg...) printk("[OV5650]"x,##arg)
#define vfe_dev_print(x,arg...) printk("[OV5650]"x,##arg)

#define LOG_ERR_RET(x)  { \
                          int ret;  \
                          ret = x; \
                          if(ret < 0) {\
                            vfe_dev_err("error at %s\n",__func__);  \
                            return ret; \
                          } \
                        }

//define module timing
#define MCLK              (24*1000*1000)
#define VREF_POL          V4L2_MBUS_VSYNC_ACTIVE_LOW
#define HREF_POL          V4L2_MBUS_HSYNC_ACTIVE_HIGH


#define CLK_POL           V4L2_MBUS_PCLK_SAMPLE_RISING
#define V4L2_IDENT_SENSOR 0x5650

//define the voltage level of control signal
#define CSI_STBY_ON     1
#define CSI_STBY_OFF    0
#define CSI_RST_ON      0
#define CSI_RST_OFF     1
#define CSI_PWR_ON      1
#define CSI_PWR_OFF     0
#define CSI_AF_PWR_ON   1
#define CSI_AF_PWR_OFF  0
#define regval_list reg_list_a16_d8


#define REG_TERM 0xfffe
#define VAL_TERM 0xfe
#define REG_DLY  0xffff

/*
 * Our nominal (default) frame rate.
 */
#ifdef FPGA
#define SENSOR_FRAME_RATE 15
#else
#define SENSOR_FRAME_RATE 30
#endif

/*
 * The ov5650 sits on i2c with ID 0x6c
 */
#define I2C_ADDR 0x6c
#define SENSOR_NAME "ov5650"
//static struct delayed_work sensor_s_ae_ratio_work;
static struct v4l2_subdev *glb_sd;

/*
 * Information we maintain about a known sensor.
 */
struct sensor_format_struct;  /* coming later */

struct cfg_array { /* coming later */
	struct regval_list * regs;
	int size;
};

static inline struct sensor_info *to_state(struct v4l2_subdev *sd)
{
  return container_of(sd, struct sensor_info, sd);
}


/*
 * The default register settings
 *
 */


static struct regval_list sensor_default_regs[] = {
  {0x3008, 0x82},
  {REG_DLY, 0x05},
  {0x3008, 0x42},
  {0x3030, 0x10}, //bit4=1 bypass internal dvdd ldo
  {0x3103, 0x93},
  {0x3b07, 0x0c},
  {0x3017, 0xff},
  {0x3018, 0xfc},
  {0x3706, 0x41},
  {0x3630, 0x22},
  {0x3605, 0x04},
  {0x3606, 0x3f},
  {0x3712, 0x13},
  {0x370e, 0x00},
  {0x370b, 0x40},
  {0x3600, 0x54},
  {0x3601, 0x05},
  {0x3713, 0x22},
  {0x3714, 0x27},
  {0x3631, 0x22},
  {0x3612, 0x1a},
  {0x3604, 0x40},
  {0x3710, 0x28},
  {0x3702, 0x3a},
  {0x3704, 0x18},
  {0x3a18, 0x00},
  {0x3a19, 0xf8},
  {0x3a00, 0x38},
  {0x380c, 0x0c},
  {0x380d, 0xb4},
  {0x380e, 0x07},
  {0x380f, 0xb0},
  {0x3830, 0x50},
  {0x3a08, 0x12},
  {0x3a09, 0x70},
  {0x3a0a, 0x0f},
  {0x3a0b, 0x60},
  {0x3a0d, 0x06},
  {0x3a0e, 0x06},
  {0x3a13, 0x54},
  {0x3815, 0x82},
  {0x5059, 0x80},
  {0x3615, 0x52},
  {0x505a, 0x0a},
  {0x505b, 0x2e},
  {0x3826, 0x00},
  {0x3a1a, 0x06},
  //{0x3503, 0x00},
  {0x3623, 0x01},
  {0x3633, 0x24},
  {0x3c01, 0x34},
  {0x3c04, 0x28},
  {0x3c05, 0x98},
  {0x3c07, 0x07},
  {0x3c09, 0xc2},
  {0x4000, 0x05},
  {0x4001, 0x02},
  {0x5046, 0x01},
  {0x3810, 0x40},
  {0x3836, 0x41},
  {0x505f, 0x04},
  {0x5000, 0x00},
  {0x5001, 0x00},
  {0x503d, 0x00},
  {0x585a, 0x01},
  {0x585b, 0x2c},
  {0x585c, 0x01},
  {0x585d, 0x93},
  {0x585e, 0x01},
  {0x585f, 0x90},
  {0x5860, 0x01},
  {0x5861, 0x0d},
  {0x5180, 0xc0},
  {0x5184, 0x00},
  {0x470a, 0x00},
  {0x470b, 0x00},
  {0x470c, 0x00},
  {0x300f, 0x8e},
  {0x3603, 0xa7},
  {0x3632, 0x55},
  {0x3620, 0x56},
  {0x3631, 0x36},
  {0x3632, 0x5f},
  {0x3711, 0x24},
  {0x401f, 0x03},
  {0x3a0f, 0x78},
  {0x3a10, 0x68},
  {0x3a1b, 0x78},
  {0x3a1e, 0x68},
  {0x3a11, 0xd0},
  {0x3a1f, 0x40},
  {0x3503, 0x13}, //manual AE AGC
  {0x5001, 0x01}, //manual AWB
  {0x5046, 0x09},
  {0x3406, 0x01},
  {0x3400, 0x04},
  {0x3401, 0x00},
  {0x3402, 0x04},
  {0x3403, 0x00},
  {0x3404, 0x04},
  {0x3405, 0x00},
  {0x5000, 0x06}, //[7]lenc off,[1:0]bc/wc on
  {0x3008, 0x02},  
  //{REG_TERM,VAL_TERM},
};                                                           

//for capture                                                                         
static struct regval_list sensor_qsxga_regs[] = { //qsxga: 2592*1936@7.5fps 48MHz
#if 0
	//capture 5Mega 7.5fps
  {0x3008, 0x42},
  {0x3613, 0x44},//?
  {0x370d, 0x04},//|
  {0x3703, 0xe6},//|
  {0x3705, 0xda},//|
  {0x370a, 0x80},//
  {0x370c, 0x00},
  {0x3703, 0xe6},
  {0x3713, 0x22},
  {0x3714, 0x27},//analog ctrl
  {0x3010, 0x30},//PLL1
  {0x3011, 0x10},//PLL2
  {0x3012, 0x02},//PLL3
  {0x3800, 0x02},//HREF start[11:8]
  {0x3801, 0x54},//HREF start[7:0]
  {0x3802, 0x00},//VREF start[11:8]
  {0x3803, 0x0c},//VREF start[7:0]
  {0x3804, 0x0a},//HREF width[11:8]
  {0x3805, 0x20},//HREF width[7:0] 
  {0x3806, 0x07},//VREF width[11:8]
  {0x3807, 0x98},//VREF width[7:0] 
  {0x3808, 0x0a},//DVP H output[11:8]
  {0x3809, 0x20},//DVP H output[7:0] 
  {0x380a, 0x07},//DVP V output[11:8]
  {0x380b, 0x98},//DVP V output[7:0] 
  {0x380c, 0x0c},//HTS[11:8]
  {0x380d, 0xb4},//HTS[7:0]
  {0x380e, 0x07},//VTS[11:8] 0x03
  {0x380f, 0xb0},//VTS[7:0] 0xEC
  {0x3815, 0x82},//[4:0]pclk2sclk ratio
  {0x3818, 0xc0},//[6:5]mirro/flip [1:0]vsub4/2
  {0x3819, 0x80},//[7:4]SOF to HREF delay(count in lines) [1:0]vts ctrl
  {0x381a, 0x4a},//HS mirror adj
  {0x381c, 0x20},//[7]vga preview mode [3:0]tc_vs_crop_l
  {0x381d, 0x0a},//tc_vs_crop_h
  {0x381e, 0x01},//vh_crop_h
  {0x381f, 0x20},//vh_crop_l
  {0x3820, 0x00},
  {0x3821, 0x00},
  {0x3824, 0x01},
  {0x3825, 0xb4},
  {0x3827, 0x0a},
  {0x3503, 0x03},//[5:4] gain delay frame
  {0x350c, 0x00},//vts_diff[15:8] manual mode set to 0
  {0x350d, 0x00},//vts_diff[7:0] manual mode set to 0
  {0x3a01, 0x00},//min exp??
  {0x3a00, 0x38},//[6:0] LOL/band_en/band_low_limit/m.n_en/debug/freeze
  {0x3a13, 0x54},//[5][4:0]pre_gain en/value
  {0x401d, 0x28},//
  {0x401c, 0x46},//?
  {0x5002, 0x00},//[1] VAP_en
  {0x5900, 0x00},//VAP ctrl
  {0x5901, 0x00},//VAP ctrl1
  {0x3621, 0x2f},//?
  {0x3008, 0x02}, 
#endif
	//capture 5Mega 7.5fps
  {0x3008, 0x42},
  {0x3613, 0x44},//?
  {0x370d, 0x04},//|
  {0x3703, 0xe6},//|
  {0x3705, 0xda},//|
  {0x370a, 0x80},//
  {0x370c, 0x00},
  {0x3703, 0xe6},
  {0x3713, 0x22},
  {0x3714, 0x27},//analog ctrl
  {0x3010, 0x30},//PLL1
  {0x3011, 0x10},//PLL2
  {0x3012, 0x00},//PLL3
  {0x3800, 0x02},//HREF start[11:8]
  {0x3801, 0x54},//HREF start[7:0]
  {0x3802, 0x00},//VREF start[11:8]
  {0x3803, 0x0c},//VREF start[7:0]
  {0x3804, 0x0a},//HREF width[11:8]
  {0x3805, 0x20},//HREF width[7:0] 
  {0x3806, 0x07},//VREF width[11:8]
  {0x3807, 0x98},//VREF width[7:0] 
  {0x3808, 0x0a},//DVP H output[11:8]
  {0x3809, 0x20},//DVP H output[7:0] 
  {0x380a, 0x07},//DVP V output[11:8]
  {0x380b, 0x98},//DVP V output[7:0] 
  {0x380c, 0x0c},//HTS[11:8]
  {0x380d, 0xb4},//HTS[7:0]
  {0x380e, 0x07},//VTS[11:8] 0x03
  {0x380f, 0xb0},//VTS[7:0] 0xEC
  {0x3815, 0x82},//[4:0]pclk2sclk ratio
  {0x3818, 0xc0},//[6:5]mirro/flip [1:0]vsub4/2
  {0x3819, 0x80},//[7:4]SOF to HREF delay(count in lines) [1:0]vts ctrl
  {0x381a, 0x4a},//HS mirror adj
  {0x381c, 0x20},//[7]vga preview mode [3:0]tc_vs_crop_l
  {0x381d, 0x0a},//tc_vs_crop_h
  {0x381e, 0x01},//vh_crop_h
  {0x381f, 0x20},//vh_crop_l
  {0x3820, 0x00},
  {0x3821, 0x00},
  {0x3824, 0x01},
  {0x3825, 0xb4},
  {0x3827, 0x0a},
  {0x3503, 0x13},//[5:4] gain delay frame  
  {0x350c, 0x00},//vts_diff[15:8] manual mode set to 0
  {0x350d, 0x00},//vts_diff[7:0] manual mode set to 0
  {0x3a01, 0x00},//min exp??
  {0x3a00, 0x38},//[6:0] LOL/band_en/band_low_limit/m.n_en/debug/freeze
  {0x3a13, 0x54},//[5][4:0]pre_gain en/value
  {0x401d, 0x28},//
  {0x401c, 0x46},//?
  {0x5002, 0x00},//[1] VAP_en
  {0x5900, 0x00},//VAP ctrl
  {0x5901, 0x00},//VAP ctrl1
  {0x3621, 0x2f},//?
  {0x3008, 0x02},   
  {0x5000, 0x00},
  {0x5001, 0x00},
  {0x5002, 0x00},
  {0x5046, 0x00},
  //{REG_TERM,VAL_TERM},
};

//static struct regval_list sensor_qxga_regs[] = { //qxga: 2048*1536
//         
//};                                      
//
//static struct regval_list sensor_uxga_regs[] = { //UXGA: 1600*1200
// 
//};

static struct regval_list sensor_sxga_regs[] = { //SXGA: 1280*960@30fps //64MHz pclk
//  {0x3008,0x42},
  {0x3613,0xc4},//?
  {0x370d,0x42},//|
  {0x3703,0x9a},//|
  {0x3705,0xdb},//|
  {0x370a,0x81},//
  {0x370c,0x00},
  {0x3703,0x9a},
  {0x3713,0x92},
  {0x3714,0x17},//analog ctrl
  {0x3010,0x30},//PLL1  0x10->96MHz 0x20 64MHz 0x30 48MHz
  {0x3011,0x10},//PLL2
  {0x3012,0x02},//PLL3 0x02->30fps
  {0x3800,0x03},//HREF start[11:8]
  {0x3801,0x34},//HREF start[7:0]
  {0x3802,0x00},//VREF start[11:8]
  {0x3803,0x0b},//VREF start[7:0]//0x0c
  {0x3804,0x05},//HREF width[11:8]
  {0x3805,0x10},//HREF width[7:0] 
  {0x3806,0x03},//VREF width[11:8]
  {0x3807,0xcc},//VREF width[7:0] 
  {0x3808,0x05},//DVP H output[11:8]
  {0x3809,0x00},//DVP H output[7:0] 
  {0x380a,0x03},//DVP V output[11:8]
  {0x380b,0xc0},//DVP V output[7:0] 
  {0x380c,0x08},//HTS[11:8]
  {0x380d,0x78},//HTS[7:0]
  {0x380e,0x03},//VTS[11:8] 0x03
  {0x380f,0xD8},//VTS[7:0] 0xEC
  {0x3815,0x81},//[4:0]pclk2sclk ratio
  {0x3818,0xc1},//[6:5]mirro/flip [1:0]vsub4/2
  {0x3819,0x80},//[7:4]SOF to HREF delay(count in lines) [1:0]vts ctrl
  {0x381a,0x4a},//HS mirror adj//0x381b,0x4a,//VS flip adj
  {0x381c,0x20},//[7]vga preview mode [3:0]tc_vs_crop_l
  {0x381d,0x0a},//tc_vs_crop_h
  {0x381e,0x01},//vh_crop_h
  {0x381f,0x20},//vh_crop_l
  {0x3820,0x00},
  {0x3821,0x00},
  {0x3824,0x01},
  {0x3825,0xb4},
  {0x3827,0x0a},
  {0x3503,0x13},//[5:4] gain delay frame
  {0x350c,0x00},//vts_diff[15:8] manual mode set to 0
  {0x350d,0x00},//vts_diff[7:0] manual mode set to 0
  {0x3a01,0x00},//min exp??
  {0x3a00,0x38},//[6:0] LOL/band_en/band_low_limit/m.n_en/debug/freeze
  {0x3a13,0x54},//[5][4:0]pre_gain en/value
  {0x401d,0x08},//
  {0x401c,0x42},//?
  {0x5002,0x00},//[1] VAP_en
  {0x5900,0x00},//VAP ctrl
  {0x5901,0x00},//VAP ctrl1
  {0x3621,0xaf},//?
  {0x3008,0x02},  
  //{REG_TERM,VAL_TERM},
};

//static struct regval_list sensor_xga_regs[] = { //XGA: 1024*768
//};

//for video
static struct regval_list sensor_1080p_regs[] = { //1080: 1920*1080@30fps //96MHz pclk
//  {0x3008,0x42},
  {0x3613,0x44},//?
  {0x370d,0x04},//|
  {0x3703,0xe6},//|
  {0x3705,0xda},//|
  {0x370a,0x80},//
  {0x370c,0x00},
  {0x3703,0xe6},
  {0x3713,0x22},
  {0x3714,0x27},//analog ctrl
  {0x3010,0x10},//PLL1  0x10->96MHz 0x20 64MHz 0x30 48MHz
  {0x3011,0x10},//PLL2
  {0x3012,0x02},//PLL3
  {0x3800,0x02},//HREF start[11:8]
  {0x3801,0x94},//HREF start[7:0]
  {0x3802,0x00},//VREF start[11:8]
  {0x3803,0x0a},//VREF start[7:0]//0x0c
  {0x3804,0x07},//HREF width[11:8]
  {0x3805,0x80},//HREF width[7:0] 
  {0x3806,0x04},//VREF width[11:8]
  {0x3807,0x38},//VREF width[7:0] 
  {0x3808,0x07},//DVP H output[11:8]
  {0x3809,0x80},//DVP H output[7:0] 
  {0x380a,0x04},//DVP V output[11:8]
  {0x380b,0x38},//DVP V output[7:0] 
  {0x380c,0x0a},//HTS[11:8]
  {0x380d,0x84},//HTS[7:0]
  {0x380e,0x04},//VTS[11:8] 0x03
  {0x380f,0xa4},//VTS[7:0] 0xEC
  {0x3815,0x82},//[4:0]pclk2sclk ratio
  {0x3818,0xc0},//[6:5]mirro/flip [1:0]vsub4/2
  {0x3819,0x80},//[7:4]SOF to HREF delay(count in lines) [1:0]vts ctrl
  {0x381a,0x1c},//HS mirror adj//0x381b,0x4a,//VS flip adj
  {0x381c,0x31},//[7]vga preview mode [3:0]tc_vs_crop_l
  {0x381d,0xa4},//tc_vs_crop_h
  {0x381e,0x04},//vh_crop_h
  {0x381f,0x60},//vh_crop_l
  {0x3820,0x03},
  {0x3821,0x1a},
  {0x3824,0x01},
  {0x3825,0xb4},
  {0x3827,0x0a},
  {0x3503,0x13},//[5:4] gain delay frame
  {0x350c,0x00},//vts_diff[15:8] manual mode set to 0
  {0x350d,0x00},//vts_diff[7:0] manual mode set to 0
  {0x3a01,0x00},//min exp??
  {0x3a00,0x38},//[6:0] LOL/band_en/band_low_limit/m.n_en/debug/freeze
  {0x3a13,0x54},//[5][4:0]pre_gain en/value
  {0x401d,0x28},//
  {0x401c,0x46},//?
  {0x5002,0x00},//[1] VAP_en
  {0x5900,0x00},//VAP ctrl
  {0x5901,0x00},//VAP ctrl1
  {0x3621,0x2f},//?
  {0x3008,0x02},
  //{REG_TERM,VAL_TERM},
};

static struct regval_list sensor_720p_regs[] = { //720: 1280*720@30fps //48MHz pclk
//  {0x3008,0x42},
  {0x3613,0xc4},//?
  {0x370d,0x42},//|
  {0x3703,0x9a},//|
  {0x3705,0xdb},//|
  {0x370a,0x81},//
  {0x370c,0x00},
  {0x3703,0x9a},
  {0x3713,0x92},
  {0x3714,0x17},//analog ctrl
  {0x3010,0x30},//PLL1  0x10->96MHz 0x20 64MHz 0x30 48MHz
  {0x3011,0x10},//PLL2
  {0x3012,0x02},//PLL3
  {0x3800,0x02},//HREF start[11:8]
  {0x3801,0x54},//HREF start[7:0]
  {0x3802,0x00},//VREF start[11:8]
  {0x3803,0xf8},//VREF start[7:0]//0x0c
  {0x3804,0x05},//HREF width[11:8]
  {0x3805,0x00},//HREF width[7:0] 
  {0x3806,0x02},//VREF width[11:8]
  {0x3807,0xd0},//VREF width[7:0] 
  {0x3808,0x05},//DVP H output[11:8]
  {0x3809,0x00},//DVP H output[7:0] 
  {0x380a,0x02},//DVP V output[11:8]
  {0x380b,0xd0},//DVP V output[7:0] 
  {0x380c,0x08},//HTS[11:8]
  {0x380d,0x72},//HTS[7:0]
  {0x380e,0x02},//VTS[11:8]
  {0x380f,0xe4},//VTS[7:0]
  {0x3815,0x81},//[4:0]pclk2sclk ratio
  {0x3818,0xc1},//[6:5]mirro/flip [1:0]vsub4/2
  {0x3819,0x80},//[7:4]SOF to HREF delay(count in lines) [1:0]vts ctrl
  {0x381a,0x00},//HS mirror adj//0x381b,0x4a,//VS flip adj
  {0x381c,0x10},//[7]vga preview mode [3:0]tc_vs_crop_l
  {0x381d,0x82},//tc_vs_crop_h
  {0x381e,0x05},//vh_crop_h
  {0x381f,0xc0},//vh_crop_l
  {0x3820,0x00},
  {0x3821,0x20},
  {0x3824,0x23},
  {0x3825,0x2c},
  {0x3827,0x0c},
  {0x3503,0x13},//[5:4] gain delay frame
  {0x350c,0x00},//vts_diff[15:8] manual mode set to 0
  {0x350d,0x00},//vts_diff[7:0] manual mode set to 0
  {0x3a01,0x00},//min exp??
  {0x3a00,0x38},//[6:0] LOL/band_en/band_low_limit/m.n_en/debug/freeze
  {0x3a13,0x54},//[5][4:0]pre_gain en/value
  {0x401d,0x28},//
  {0x401c,0x42},//?
  {0x5002,0x00},//[1] VAP_en
  {0x5900,0x00},//VAP ctrl
  {0x5901,0x00},//VAP ctrl1
  {0x3621,0xaf},//?
  {0x3008,0x02},
  //{REG_TERM,VAL_TERM},
};

//static struct regval_list sensor_svga_regs[] = { //SVGA: 800*600
//
//};
//
//static struct regval_list sensor_vga_regs[] = { //VGA:  640*480
//  
//};

//misc
static struct regval_list sensor_oe_disable_regs[] = {
  {0x3017,0x00},
  {0x3018,0x00},
  //{REG_TERM,VAL_TERM},
};

static struct regval_list sensor_oe_enable_regs[] = {
  {0x3017,0x7f},
  {0x3018,0xfc},
  //{REG_TERM,VAL_TERM},
};

/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 * 
 */

static struct regval_list sensor_fmt_raw[] = {
  {0x3508, 0x00},//long gain[8]
  {0x3509, 0x00},//long gain[7:0]
  {0x350a, 0x00},//
  {0x350b, 0x00},//gain=2X
  {0x3500, 0x00},//long exp[19:16] unit in 1/16 line
  {0x3501, 0x40},//long exp[15:8]
  {0x3502, 0xf0},//long exp[7:0]
  //{REG_TERM,VAL_TERM},
};

/*
 * Low-level register I/O.
 *
 */


/*
 * On most platforms, we'd rather do straight i2c I/O.
 */
static int sensor_read(struct v4l2_subdev *sd, unsigned short reg,
    unsigned char *value)
{
	int ret=0;
	int cnt=0;
	
  ret = cci_read_a16_d8(sd,reg,value);
  while(ret!=0&&cnt<2)
  {
  	ret = cci_read_a16_d8(sd,reg,value);
  	cnt++;
  }
  if(cnt>0)
  	vfe_dev_dbg("sensor read retry=%d\n",cnt);
  
  return ret;
}

static int sensor_write(struct v4l2_subdev *sd, unsigned short reg,
    unsigned char value)
{
	int ret=0;
	int cnt=0;
	
  ret = cci_write_a16_d8(sd,reg,value);
  while(ret!=0&&cnt<2)
  {
  	ret = cci_write_a16_d8(sd,reg,value);
  	cnt++;
  }
  if(cnt>0)
  	vfe_dev_dbg("sensor write retry=%d\n",cnt);
  
  return ret;
}

/*
 * Write a list of register settings;
 */
static int sensor_write_array(struct v4l2_subdev *sd, struct regval_list *regs, int array_size)
{
	int i=0;
	
  if(!regs)
  	return -EINVAL;
  
  while(i<array_size)
  {
    if(regs->addr == REG_DLY) {
      msleep(regs->data);
    } 
    else {  
    	//printk("write 0x%x=0x%x\n", regs->addr, regs->data);
      LOG_ERR_RET(sensor_write(sd, regs->addr, regs->data))
    }
    i++;
    regs++;
  }
  return 0;
}

/*
 * Write a list of continuous register setting;
 */
//static int sensor_write_continuous(struct v4l2_subdev *sd, unsigned short addr, unsigned char vals[] , uint size)
//{
//  struct i2c_client *client = v4l2_get_subdevdata(sd);
//  struct i2c_msg msg;
//  unsigned char data[2+32];
//  unsigned char *p = vals;
//  int ret,i;
//  
//  while (size > 0) {
//    int len = size > 32 ? 32 : size;
//    data[0] = (addr&0xff00) >> 8;
//    data[1] = (addr&0x00ff);
//    
//    for(i = 2; i < 2+len; i++)
//      data[i] = *p++;
//
//    msg.addr = client->addr;
//    msg.flags = 0;  
//    msg.len = 2+len;
//    msg.buf = data;
//  
//    ret = i2c_transfer(client->adapter, &msg, 1);
//    
//    if (ret > 0) {
//      ret = 0;
//    } else if (ret < 0) {
//      vfe_dev_err("sensor_write error!\n");
//    }
//    addr += len;
//    size -= len;
//  }
//  return ret;
//}


/* 
 * Code for dealing with controls.
 * fill with different sensor module
 * different sensor module has different settings here
 * if not support the follow function ,retrun -EINVAL
 */

/* *********************************************begin of ******************************************** */
/*
static int sensor_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
  struct sensor_info *info = to_state(sd);
  unsigned char rdval;
    
  LOG_ERR_RET(sensor_read(sd, 0x3821, &rdval))
  
  rdval &= (1<<1);
  rdval >>= 1;
    
  *value = rdval;

  info->hflip = *value;
  return 0;
}

static int sensor_s_hflip(struct v4l2_subdev *sd, int value)
{
  struct sensor_info *info = to_state(sd);
  unsigned char rdval;
  
  if(info->hflip == value)
    return 0;
    
  LOG_ERR_RET(sensor_read(sd, 0x3821, &rdval))
  
  switch (value) {
    case 0:
      rdval &= 0xf9;
      break;
    case 1:
      rdval |= 0x06;
      break;
    default:
      return -EINVAL;
  }
  
  LOG_ERR_RET(sensor_write(sd, 0x3821, rdval))
  
  usleep_range(10000,12000);
  info->hflip = value;
  return 0;
}

static int sensor_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
  struct sensor_info *info = to_state(sd);
  unsigned char rdval;
  
  LOG_ERR_RET(sensor_read(sd, 0x3820, &rdval))
  
  rdval &= (1<<1);  
  *value = rdval;
  rdval >>= 1;
  
  info->vflip = *value;
  return 0;
}

static int sensor_s_vflip(struct v4l2_subdev *sd, int value)
{
  struct sensor_info *info = to_state(sd);
  unsigned char rdval;
  
  if(info->vflip == value)
    return 0;
  
  LOG_ERR_RET(sensor_read(sd, 0x3820, &rdval))

  switch (value) {
    case 0:
      rdval &= 0xf9;
      break;
    case 1:
      rdval |= 0x06;
      break;
    default:
      return -EINVAL;
  }

  LOG_ERR_RET(sensor_write(sd, 0x3820, rdval))
  
  usleep_range(10000,12000);
  info->vflip = value;
  return 0;
}
*/
static int sensor_g_exp(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	
	*value = info->exp;
	vfe_dev_dbg("sensor_get_exposure = %d\n", info->exp);
	return 0;
}

static int sensor_s_exp(struct v4l2_subdev *sd, unsigned int exp_val)
{
	unsigned char explow,expmid,exphigh;
	struct sensor_info *info = to_state(sd);

//	vfe_dev_dbg("sensor_set_exposure = %d\n", exp_val>>4);
	if(exp_val>0xfffff)
		exp_val=0xfffff;
	
    exphigh = (unsigned char) ( (0x0f0000&exp_val)>>16);
    expmid  = (unsigned char) ( (0x00ff00&exp_val)>>8);
    explow  = (unsigned char) ( (0x0000ff&exp_val)   );
	
	sensor_write(sd, 0x3502, explow);
	sensor_write(sd, 0x3501, expmid);
	sensor_write(sd, 0x3500, exphigh);	
	//printk("5650 sensor_set_exp = %d, Done!\n", exp_val);
	
	info->exp = exp_val;
	return 0;
}

enum ov5647_clk_div {
  CLK_DIV_BY_1        = 0,
  CLK_DIV_BY_1_dot_5  = 1,
  CLK_DIV_BY_2        = 2,
  CLK_DIV_BY_2_dot_5  = 3,
  CLK_DIV_BY_3        = 4,
  CLK_DIV_BY_4        = 5,
  CLK_DIV_BY_6        = 6,
  CLK_DIV_BY_8        = 7,
};

int frame_rate_relat[] = {120,80,60,48,40,30,20,15};

static int sensor_s_framerate(struct v4l2_subdev *sd, unsigned int frame_rate)
{
    int set_clk_div;
//	struct sensor_info *info = to_state(sd);

    switch(frame_rate)
    {
    	case 120:
			set_clk_div = CLK_DIV_BY_1;
			break;
		case 80:
			set_clk_div = CLK_DIV_BY_1_dot_5;
			break;
		case 60:
			set_clk_div = CLK_DIV_BY_2;
			break;
		case 48:
			set_clk_div = CLK_DIV_BY_2_dot_5;
			break;
		case 40:
			set_clk_div = CLK_DIV_BY_3;
			break;
		case 30:
			set_clk_div = CLK_DIV_BY_4;
			break;
		case 20:
			set_clk_div = CLK_DIV_BY_6;
			break;
		case 15:
			set_clk_div = CLK_DIV_BY_8;
			break;
		default:
			set_clk_div = CLK_DIV_BY_1;
			break;		
    }

	//printk("set_clk_div = %d\n",set_clk_div);

	sensor_write(sd,0x3012,set_clk_div);
	
	return 0;
}


static int sensor_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	struct sensor_info *info = to_state(sd);
	
	*value = info->gain;
	vfe_dev_dbg("sensor_get_gain = %d\n", info->gain);
	return 0;
}

static int sensor_s_gain(struct v4l2_subdev *sd, int gain_val)
{
	struct sensor_info *info = to_state(sd);
	unsigned char gainlow=0;
	unsigned char gainhigh=0;
	unsigned char gainlow_l4b=0;
	unsigned int tmp_gain_val=0;
	
//	if(gain_val<1*16)
//		gain_val=16;
//	if(gain_val>64*16)
//		gain_val=64*16;
	
	tmp_gain_val=gain_val;
	
	//determine ?gain_val>31
	if(tmp_gain_val>31)
	{
		gainlow |= 0x10;
		tmp_gain_val = tmp_gain_val>>1;
	}
	//determine ?gain_val>2*31
	if(tmp_gain_val>31)
	{
		gainlow |= 0x20;
		tmp_gain_val = tmp_gain_val>>1;
	}
	//determine ?gain_val>4*31
	if(tmp_gain_val>31)
	{
		gainlow |= 0x40;
		tmp_gain_val = tmp_gain_val>>1;
	}
	//determine ?gain_val>8*31
	if(tmp_gain_val>31)
	{
		gainlow |= 0x80;
		tmp_gain_val = tmp_gain_val>>1;
	}
	//determine ?gain_val>16*31
	if(tmp_gain_val>31)
	{
		gainhigh = 0x01;
		tmp_gain_val = tmp_gain_val>>1;
	}
	
	if(tmp_gain_val>16)
		gainlow_l4b=(tmp_gain_val-16)&0x0f;
	
	gainlow  = gainlow | gainlow_l4b;

	
	sensor_write(sd, 0x350b, gainlow);
	sensor_write(sd, 0x350a, gainhigh);
	
	//printk("5650 sensor_set_gain = %d, Done!\n", gain_val);
	info->gain = gain_val;
	
	return 0;
}

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	int ret;
	unsigned char rdval;
	
	ret=sensor_read(sd, 0x3008, &rdval);
	if(ret!=0)
		return ret;
	
	if(on_off==CSI_STBY_ON)//sw stby on
	{
		ret=sensor_write(sd, 0x3008, rdval|0x40);
	}
	else//sw stby off
	{
		ret=sensor_write(sd, 0x00, rdval&0xbf);
	}
	return ret;
}


/*
 * Stuff that knows about the sensor.
 */
 
static int sensor_power(struct v4l2_subdev *sd, int on)
{
  int ret;
  
  //insure that clk_disable() and clk_enable() are called in pair 
  //when calling CSI_SUBDEV_STBY_ON/OFF and CSI_SUBDEV_PWR_ON/OFF
  ret = 0;
  switch(on)
  {
    case CSI_SUBDEV_STBY_ON:
      vfe_dev_dbg("CSI_SUBDEV_STBY_ON!\n");
      //disable io oe
      vfe_dev_print("disalbe oe!\n");
      ret = sensor_write_array(sd, sensor_oe_disable_regs, ARRAY_SIZE(sensor_oe_disable_regs));
      if(ret < 0)
        vfe_dev_err("disalbe oe falied!\n");
      //software standby on
      ret = sensor_s_sw_stby(sd, CSI_STBY_ON);
      if(ret < 0)
        vfe_dev_err("soft stby falied!\n");
      usleep_range(10000,12000);
      //make sure that no device can access i2c bus during sensor initial or power down
      //when using i2c_lock_adpater function, the following codes must not access i2c bus before calling i2c_unlock_adapter
      cci_lock(sd);
      //standby on io
      vfe_gpio_write(sd,PWDN,CSI_STBY_ON);
      //remember to unlock i2c adapter, so the device can access the i2c bus again
      cci_unlock(sd);  
      //inactive mclk after stadby in
      vfe_set_mclk(sd,OFF);
      break;
    case CSI_SUBDEV_STBY_OFF:
      vfe_dev_dbg("CSI_SUBDEV_STBY_OFF!\n");
      //make sure that no device can access i2c bus during sensor initial or power down
      //when using i2c_lock_adpater function, the following codes must not access i2c bus before calling i2c_unlock_adapter
      cci_lock(sd);    
      //active mclk before stadby out
      vfe_set_mclk_freq(sd,MCLK);
      vfe_set_mclk(sd,ON);
      usleep_range(10000,12000);
      //standby off io
      vfe_gpio_write(sd,PWDN,CSI_STBY_OFF);
      usleep_range(10000,12000);
      //remember to unlock i2c adapter, so the device can access the i2c bus again
      cci_unlock(sd);        
      //software standby
      ret = sensor_s_sw_stby(sd, CSI_STBY_OFF);
      if(ret < 0)
        vfe_dev_err("soft stby off falied!\n");
      usleep_range(10000,12000);
//      vfe_dev_print("enable oe!\n");
//      ret = sensor_write_array(sd, sensor_oe_enable_regs);
//      if(ret < 0)
//        vfe_dev_err("enable oe falied!\n");
      break;
    case CSI_SUBDEV_PWR_ON:
      vfe_dev_dbg("CSI_SUBDEV_PWR_ON!\n");
      //make sure that no device can access i2c bus during sensor initial or power down
      //when using i2c_lock_adpater function, the following codes must not access i2c bus before calling i2c_unlock_adapter
      cci_lock(sd);
      //power on reset
      vfe_gpio_set_status(sd,PWDN,1);//set the gpio to output
      vfe_gpio_set_status(sd,RESET,1);//set the gpio to output
      //power down io
      vfe_gpio_write(sd,PWDN,CSI_STBY_ON);
      //reset on io
      vfe_gpio_write(sd,RESET,CSI_RST_ON);
      usleep_range(1000,1200);
      //active mclk before power on
      vfe_set_mclk_freq(sd,MCLK);
      vfe_set_mclk(sd,ON);
      usleep_range(10000,12000);
      //power supply
      vfe_gpio_write(sd,POWER_EN,CSI_PWR_ON);
      vfe_set_pmu_channel(sd,IOVDD,ON);
      vfe_set_pmu_channel(sd,AVDD,ON);
      vfe_set_pmu_channel(sd,DVDD,ON);
      vfe_set_pmu_channel(sd,AFVDD,ON);
      //standby off io
      vfe_gpio_write(sd,PWDN,CSI_STBY_OFF);
      usleep_range(10000,12000);
      //reset after power on
      vfe_gpio_write(sd,RESET,CSI_RST_OFF);
      usleep_range(30000,31000);
      //remember to unlock i2c adapter, so the device can access the i2c bus again
      cci_unlock(sd);  
      break;
    case CSI_SUBDEV_PWR_OFF:
      vfe_dev_dbg("CSI_SUBDEV_PWR_OFF!\n");
      //make sure that no device can access i2c bus during sensor initial or power down
      //when using i2c_lock_adpater function, the following codes must not access i2c bus before calling i2c_unlock_adapter
      cci_lock(sd);
      //inactive mclk before power off
      vfe_set_mclk(sd,OFF);
      //power supply off
      vfe_gpio_write(sd,POWER_EN,CSI_PWR_OFF);
      vfe_set_pmu_channel(sd,AFVDD,OFF);
      vfe_set_pmu_channel(sd,DVDD,OFF);
      vfe_set_pmu_channel(sd,AVDD,OFF);
      vfe_set_pmu_channel(sd,IOVDD,OFF);  
      //standby and reset io
      usleep_range(10000,12000);
      vfe_gpio_write(sd,POWER_EN,CSI_STBY_OFF);
      vfe_gpio_write(sd,RESET,CSI_RST_ON);
      //set the io to hi-z
      vfe_gpio_set_status(sd,RESET,0);//set the gpio to input
      vfe_gpio_set_status(sd,PWDN,0);//set the gpio to input
      //remember to unlock i2c adapter, so the device can access the i2c bus again
      cci_unlock(sd);  
      break;
    default:
      return -EINVAL;
  }   

  return 0;
}
 
static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
  switch(val)
  {
    case 0:
      vfe_gpio_write(sd,RESET,CSI_RST_OFF);
      usleep_range(10000,12000);
      break;
    case 1:
      vfe_gpio_write(sd,RESET,CSI_RST_ON);
      usleep_range(10000,12000);
      break;
    default:
      return -EINVAL;
  }
    
  return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
  unsigned char rdval;
  
  LOG_ERR_RET(sensor_read(sd, 0x300a, &rdval))
  if(rdval != 0x56)
    return -ENODEV;
 
  LOG_ERR_RET(sensor_read(sd, 0x300b, &rdval))
  if(rdval != 0x51)
    return -ENODEV;
  
  return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
  int ret;
  struct sensor_info *info = to_state(sd);
  
  vfe_dev_dbg("sensor_init\n");
  
  /*Make sure it is a target sensor*/
  ret = sensor_detect(sd);
  if (ret) {
    vfe_dev_err("chip found is not an target chip.\n");
    return ret;
  }
  
  vfe_get_standby_mode(sd,&info->stby_mode);
  
  if((info->stby_mode == HW_STBY || info->stby_mode == SW_STBY) \
      && info->init_first_flag == 0) {
    vfe_dev_print("stby_mode and init_first_flag = 0\n");
    return 0;
  } 
  
  info->focus_status = 0;
  info->low_speed = 0;
  info->width = QSXGA_WIDTH;
  info->height = QSXGA_HEIGHT;
  info->hflip = 0;
  info->vflip = 0;
  info->gain = 0;

  info->tpf.numerator = 1;            
  info->tpf.denominator = 30;    /* 30fps */    
  
  ret = sensor_write_array(sd, sensor_default_regs, ARRAY_SIZE(sensor_default_regs));  
  if(ret < 0) {
    vfe_dev_err("write sensor_default_regs error\n");
    return ret;
  }
  
  if(info->stby_mode == 0)
    info->init_first_flag = 0;
  
  info->preview_first_flag = 1;
  
  return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
  int ret=0;
  struct sensor_info *info = to_state(sd);
//  vfe_dev_dbg("[]cmd=%d\n",cmd);
//  vfe_dev_dbg("[]arg=%0x\n",arg);
  switch(cmd) {
    case GET_CURRENT_WIN_CFG:
      if(info->current_wins != NULL)
      {
        memcpy( arg,
                info->current_wins,
                sizeof(struct sensor_win_size) );
        ret=0;
      }
      else
      {
        vfe_dev_err("empty wins!\n");
        ret=-1;
      }
      break;
    default:
      return -EINVAL;
  }
  return ret;
}


/*
 * Store information about the video data format. 
 */
static struct sensor_format_struct {
  __u8 *desc;
  //__u32 pixelformat;
  enum v4l2_mbus_pixelcode mbus_code;
  struct regval_list *regs;
  int regs_size;
  int bpp;   /* Bytes per pixel */
}sensor_formats[] = {
	{
		.desc		= "Raw RGB Bayer",
		.mbus_code	= V4L2_MBUS_FMT_SBGGR10_1X10,
		.regs 		= sensor_fmt_raw,
		.regs_size = ARRAY_SIZE(sensor_fmt_raw),
		.bpp		= 1
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

  

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */


static struct sensor_win_size sensor_win_sizes[] = {
	  /* qsxga: 2592*1936 */
	  {
      .width      = QSXGA_WIDTH,
      .height     = QSXGA_HEIGHT,
      .hoffset    = 0,
      .voffset    = 0,
      .hts        = 3252,
      .vts        = 1968,
      .pclk       = 96*1000*1000,
      .fps_fixed  = 1,
      .bin_factor = 1,
      .intg_min   = 1<<4,
      .intg_max   = 1968<<4,
      .gain_min   = 1<<4,
      .gain_max   = 16<<4,
      .regs       = sensor_qsxga_regs,
      .regs_size  = ARRAY_SIZE(sensor_qsxga_regs),
      .set_size   = NULL,
    },
    
    /* 1080P */
    {
      .width			= HD1080_WIDTH,
      .height 		= HD1080_HEIGHT,
      .hoffset	  = 0,
      .voffset	  = 0,
      .hts        = 2692,
      .vts        = 1188,
      .pclk       = 96*1000*1000,
      .fps_fixed  = 1,
      .bin_factor = 1,
      .intg_min   = 1<<4,
      .intg_max   = 1188<<4,
      .gain_min   = 1<<4,
      .gain_max   = 16<<4,
      .regs       = sensor_1080p_regs,//
      .regs_size  = ARRAY_SIZE(sensor_1080p_regs),//
      .set_size		= NULL,
    },
	/* UXGA */
//	{
//      .width			= UXGA_WIDTH,
//      .height 		= UXGA_HEIGHT,
//      .hoffset	  = 0,
//      .voffset	  = 0,
//      .hts        = 2800,//limited by sensor
//      .vts        = 1000,
//      .pclk       = 84*1000*1000,
//      .fps_fixed  = 1,
//      .bin_factor = 1,
//      .intg_min   = ,
//      .intg_max   = ,
//      .gain_min   = ,
//      .gain_max   = ,
//      .regs			= sensor_uxga_regs,
//      .regs_size	= ARRAY_SIZE(sensor_uxga_regs),
//      .set_size		= NULL,
//	},
  	/* SXGA */
    {
      .width			= SXGA_WIDTH,
      .height 		= SXGA_HEIGHT,
      .hoffset	  = 0,
      .voffset	  = 0,
      .hts        = 2168,
      .vts        = 984,
      .pclk       = 64*1000*1000,
      .fps_fixed  = 1,
      .bin_factor = 1,
      .intg_min   = 1<<4,
      .intg_max   = 984<<4,
      .gain_min   = 1<<4,
      .gain_max   = 16<<4,
      .regs		    = sensor_sxga_regs,
      .regs_size	= ARRAY_SIZE(sensor_sxga_regs),
      .set_size		= NULL,
    },
    /* 720p */
    {
      .width			= HD720_WIDTH,
      .height 		= HD720_HEIGHT,
      .hoffset    = 0,
      .voffset    = 0,
      .hts        = 2162,
      .vts        = 740,
      .pclk       = 48*1000*1000,
      .fps_fixed  = 1,
      .bin_factor = 1,
      .intg_min   = 1<<4,
      .intg_max   = 740<<4,
      .gain_min   = 1<<4,
      .gain_max   = 16<<4,
      .regs			  = sensor_720p_regs,//
      .regs_size	= ARRAY_SIZE(sensor_720p_regs),//
      .set_size		= NULL,
    },
    /* XGA */
//    {
//      .width			= XGA_WIDTH,
//      .height 		= XGA_HEIGHT,
//      .hoffset    = 0,
//      .voffset    = 0,
//      .hts        = 2800,//limited by sensor
//      .vts        = 1000,
//      .pclk       = 84*1000*1000,
//      .fps_fixed  = 1,
//      .bin_factor = 1,
//      .intg_min   = ,
//      .intg_max   = ,
//      .gain_min   = ,
//      .gain_max   = ,
//      .regs			  = sensor_xga_regs,
//      .regs_size	= ARRAY_SIZE(sensor_xga_regs),
//      .set_size		= NULL,
//    },
  /* SVGA */
//    {
//      .width			= SVGA_WIDTH,
//      .height 		= SVGA_HEIGHT,
//      .hoffset	  = 0,
//      .voffset	  = 0,
//      .hts        = 2800,//limited by sensor
//      .vts        = 1000,
//      .pclk       = 84*1000*1000,
//      .fps_fixed  = 1,
//      .bin_factor = 1,
//      .intg_min   = ,
//      .intg_max   = ,
//      .gain_min   = ,
//      .gain_max   = ,
//      .regs       = sensor_svga_regs,
//      .regs_size  = ARRAY_SIZE(sensor_svga_regs),
//      .set_size   = NULL,
//    },
  /* VGA */
//    {
//      .width			= VGA_WIDTH,
//      .height 		= VGA_HEIGHT,
//      .hoffset	  = 0,
//      .voffset	  = 0,
//      .hts        = 2800,//limited by sensor
//      .vts        = 1000,
//      .pclk       = 84*1000*1000,
//      .fps_fixed  = 1,
//      .bin_factor = 1,
//      .intg_min   = ,
//      .intg_max   = ,
//      .gain_min   = ,
//      .gain_max   = ,
//      .regs       = sensor_vga_regs,
//      .regs_size  = ARRAY_SIZE(sensor_vga_regs),
//      .set_size   = NULL,
//    },
};

#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned index,
                 enum v4l2_mbus_pixelcode *code)
{
  if (index >= N_FMTS)
    return -EINVAL;

  *code = sensor_formats[index].mbus_code;
  return 0;
}

static int sensor_enum_size(struct v4l2_subdev *sd,
                            struct v4l2_frmsizeenum *fsize)
{
  if(fsize->index > N_WIN_SIZES-1)
  	return -EINVAL;
  
  fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
  fsize->discrete.width = sensor_win_sizes[fsize->index].width;
  fsize->discrete.height = sensor_win_sizes[fsize->index].height;
  
  return 0;
}


static int sensor_try_fmt_internal(struct v4l2_subdev *sd,
    struct v4l2_mbus_framefmt *fmt,
    struct sensor_format_struct **ret_fmt,
    struct sensor_win_size **ret_wsize)
{
  int index;
  struct sensor_win_size *wsize;
  struct sensor_info *info = to_state(sd);

  for (index = 0; index < N_FMTS; index++)
    if (sensor_formats[index].mbus_code == fmt->code)
      break;

  if (index >= N_FMTS) 
    return -EINVAL;
  
  if (ret_fmt != NULL)
    *ret_fmt = sensor_formats + index;
    
  /*
   * Fields: the sensor devices claim to be progressive.
   */
  
  fmt->field = V4L2_FIELD_NONE;
  
  /*
   * Round requested image size down to the nearest
   * we support, but not below the smallest.
   */
  for (wsize = sensor_win_sizes; wsize < sensor_win_sizes + N_WIN_SIZES;
       wsize++)
    if (fmt->width >= wsize->width && fmt->height >= wsize->height)
      break;
    
  if (wsize >= sensor_win_sizes + N_WIN_SIZES)
    wsize--;   /* Take the smallest one */
  if (ret_wsize != NULL)
    *ret_wsize = wsize;
  /*
   * Note the size we'll actually handle.
   */
  fmt->width = wsize->width;
  fmt->height = wsize->height;
  info->current_wins = wsize;
  //pix->bytesperline = pix->width*sensor_formats[index].bpp;
  //pix->sizeimage = pix->height*pix->bytesperline;

  return 0;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, 
             struct v4l2_mbus_framefmt *fmt)
{
  return sensor_try_fmt_internal(sd, fmt, NULL, NULL);
}

static int sensor_g_mbus_config(struct v4l2_subdev *sd,
           struct v4l2_mbus_config *cfg)
{
  cfg->type = V4L2_MBUS_PARALLEL;
  cfg->flags = V4L2_MBUS_MASTER | VREF_POL | HREF_POL | CLK_POL ;
  
  return 0;
}


/*
 * Set a format.
 */
static int sensor_s_fmt(struct v4l2_subdev *sd, 
             struct v4l2_mbus_framefmt *fmt)
{
  int ret;
  struct sensor_format_struct *sensor_fmt;
  struct sensor_win_size *wsize;
  struct sensor_info *info = to_state(sd);
  
  vfe_dev_dbg("sensor_s_fmt\n");
  
  sensor_write_array(sd, sensor_oe_disable_regs, ARRAY_SIZE(sensor_oe_disable_regs));
  
  ret = sensor_try_fmt_internal(sd, fmt, &sensor_fmt, &wsize);
  if (ret)
    return ret;

  if(info->capture_mode == V4L2_MODE_VIDEO)
  {
    //video
  }
  else if(info->capture_mode == V4L2_MODE_IMAGE)
  {
    //image 
    
  }

  sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

  ret = 0;
  if (wsize->regs)
    LOG_ERR_RET(sensor_write_array(sd, wsize->regs, wsize->regs_size))
  
  if (wsize->set_size)
    LOG_ERR_RET(wsize->set_size(sd))

  info->fmt = sensor_fmt;
  info->width = wsize->width;
  info->height = wsize->height;

  vfe_dev_print("s_fmt set width = %d, height = %d\n",wsize->width,wsize->height);

  if(info->capture_mode == V4L2_MODE_VIDEO)
  {
    //video
   
  } else {
    //capture image

  }
	
	sensor_write_array(sd, sensor_oe_enable_regs, ARRAY_SIZE(sensor_oe_enable_regs));
	
	return 0;
}

/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 */
static int sensor_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
  struct v4l2_captureparm *cp = &parms->parm.capture;
  struct sensor_info *info = to_state(sd);

  if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    return -EINVAL;
  
  memset(cp, 0, sizeof(struct v4l2_captureparm));
  cp->capability = V4L2_CAP_TIMEPERFRAME;
  cp->capturemode = info->capture_mode;
     
  return 0;
}

static int sensor_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
  struct v4l2_captureparm *cp = &parms->parm.capture;
  //struct v4l2_fract *tpf = &cp->timeperframe;
  struct sensor_info *info = to_state(sd);
  //unsigned char div;
  
  vfe_dev_dbg("sensor_s_parm\n");
  
  if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    return -EINVAL;
  
  if (info->tpf.numerator == 0)
    return -EINVAL;
    
  info->capture_mode = cp->capturemode;
  
  return 0;
}


static int sensor_queryctrl(struct v4l2_subdev *sd,
    struct v4l2_queryctrl *qc)
{
  /* Fill in min, max, step and default value for these controls. */
  /* see include/linux/videodev2.h for details */
  
  switch (qc->id) {
	case V4L2_CID_GAIN:
		return v4l2_ctrl_query_fill(qc, 16, 62*16, 1, 1*16);
	case V4L2_CID_EXPOSURE:
		return v4l2_ctrl_query_fill(qc, 0, 65535*16, 1, 0);
	case V4L2_CID_FRAME_RATE:
		return v4l2_ctrl_query_fill(qc, 0, 3, 1, 0);
  }
  return -EINVAL;
}

static int sensor_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
  switch (ctrl->id) {
  case V4L2_CID_GAIN:
    return sensor_g_gain(sd, &ctrl->value);
  case V4L2_CID_EXPOSURE:
  	return sensor_g_exp(sd, &ctrl->value);
  }
  return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
  struct v4l2_queryctrl qc;
  int ret;
  
  qc.id = ctrl->id;
  ret = sensor_queryctrl(sd, &qc);
  if (ret < 0) {
    return ret;
  }

  if (ctrl->value < qc.minimum || ctrl->value > qc.maximum) {
    return -ERANGE;
  }
  
  switch (ctrl->id) {
    case V4L2_CID_GAIN:
      return sensor_s_gain(sd, ctrl->value);
    case V4L2_CID_EXPOSURE:
	  return sensor_s_exp(sd, ctrl->value);
	case V4L2_CID_FRAME_RATE:
	  return sensor_s_framerate(sd, ctrl->value);
  }
  return -EINVAL;
}


static int sensor_g_chip_ident(struct v4l2_subdev *sd,
    struct v4l2_dbg_chip_ident *chip)
{
  struct i2c_client *client = v4l2_get_subdevdata(sd);

  return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SENSOR, 0);
}


/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops sensor_core_ops = {
  .g_chip_ident = sensor_g_chip_ident,
  .g_ctrl = sensor_g_ctrl,
  .s_ctrl = sensor_s_ctrl,
  .queryctrl = sensor_queryctrl,
  .reset = sensor_reset,
  .init = sensor_init,
  .s_power = sensor_power,
  .ioctl = sensor_ioctl,
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
  .enum_mbus_fmt = sensor_enum_fmt,
  .enum_framesizes = sensor_enum_size,
  .try_mbus_fmt = sensor_try_fmt,
  .s_mbus_fmt = sensor_s_fmt,
  .s_parm = sensor_s_parm,
  .g_parm = sensor_g_parm,
  .g_mbus_config = sensor_g_mbus_config,
};

static const struct v4l2_subdev_ops sensor_ops = {
  .core = &sensor_core_ops,
  .video = &sensor_video_ops,
};

/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv = {
	.name = SENSOR_NAME,
};

static int sensor_probe(struct i2c_client *client,
      const struct i2c_device_id *id)
{
  struct v4l2_subdev *sd;
  struct sensor_info *info;
//  int ret;

  info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
  if (info == NULL)
    return -ENOMEM;
  sd = &info->sd;
  glb_sd = sd;
  cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);

  info->fmt = &sensor_formats[0];
  info->af_first_flag = 1;
  info->init_first_flag = 1;

  return 0;
}


static int sensor_remove(struct i2c_client *client)
{
  struct v4l2_subdev *sd;

  sd = cci_dev_remove_helper(client, &cci_drv);
  kfree(to_state(sd));
  return 0;
}

static const struct i2c_device_id sensor_id[] = {
  { SENSOR_NAME, 0 },
  { }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);


static struct i2c_driver sensor_driver = {
  .driver = {
    .owner = THIS_MODULE,
  .name = SENSOR_NAME,
  },
  .probe = sensor_probe,
  .remove = sensor_remove,
  .id_table = sensor_id,
};
static __init int init_sensor(void)
{
	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

