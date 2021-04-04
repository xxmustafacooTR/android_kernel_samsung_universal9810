/*
 * moro_sound.h  --  Sound mod for Moon, S7 sound driver
 *
 * Author	: @morogoku https://github.com/morogoku
 *
 */


#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/printk.h>
#include <sound/soc.h>

#include <linux/mfd/madera/registers.h>


/*****************************************/
// External function declarations
/*****************************************/

void moro_sound_hook_madera_pcm_probe(struct regmap *pmap);
int _regmap_write_nohook(struct regmap *map, unsigned int reg, unsigned int val);


/*****************************************/
// Definitions
/*****************************************/

// Moro sound general
#define MORO_SOUND_DEFAULT 		0
#define MORO_SOUND_VERSION 		"2.1.1"
#define DEBUG_DEFAULT 			0

// headphone levels
#define HEADPHONE_DEFAULT		128
#define HEADPHONE_MIN 			60
#define HEADPHONE_MAX 			190

// Mixers sources
#define OUT2L_MIX_DEFAULT		32
#define OUT2R_MIX_DEFAULT		33
#define EQ1_MIX_DEFAULT			0
#define EQ2_MIX_DEFAULT			0

// EQ gain
#define EQ_DEFAULT			0
#define EQ_GAIN_DEFAULT 		0
#define EQ_GAIN_OFFSET 			12
#define EQ_GAIN_MIN 			-12
#define EQ_GAIN_MAX  			12

// Mixers
#define MADERA_MIXER_SOURCE_MASK	0xff
#define MADERA_MIXER_SOURCE_SHIFT	0
#define MADERA_MIXER_VOLUME_MASK	0xfe
#define MADERA_MIXER_VOLUME_SHIFT	1



