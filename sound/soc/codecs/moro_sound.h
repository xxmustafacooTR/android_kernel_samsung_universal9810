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
#include <sound/soc.h>

#include <linux/mfd/madera/registers.h>


/*****************************************/
// External function declarations
/*****************************************/

void moro_sound_hook_madera_pcm_probe(struct regmap *pmap);
int _regmap_write_nohook(struct regmap *map, unsigned int reg, unsigned int val);
int set_speaker_analog_gain_value(int gain);
int set_speaker_digital_gain_value(int gain);
int get_speaker_analog_gain(void);
int get_speaker_digital_gain(void);
int set_earpiece_analog_gain_value(int gain);
int set_earpiece_digital_gain_value(int gain);
int get_earpiece_analog_gain(void);
int get_earpiece_digital_gain(void);
int set_both_analog_gain_value(int gain);
int set_both_digital_gain_value(int gain);
int get_both_analog_gain(void);
int get_both_digital_gain(void);


/*****************************************/
// Definitions
/*****************************************/

// Moro sound general
#define MORO_SOUND_DEFAULT 		0
#define MORO_SOUND_VERSION 		"2.2.0"
#define DEBUG_DEFAULT 			0

// headphone levels
#define HEADPHONE_DEFAULT		128
#define HEADPHONE_MIN 			60
#define HEADPHONE_MAX 			190
#define HEADPHONE_MONO_DEFAULT		0

/* Earpiece levels */
#define EARPIECE_DEFAULT		12
#define EARPIECE_MIN 			0
#define EARPIECE_MAX 			18

#define EARPIECE_ANALOG_DEFAULT			0
#define EARPIECE_ANALOG_MIN 			0
#define EARPIECE_ANALOG_MAX 			6

#define EARPIECE_DIGITAL_DEFAULT		0
#define EARPIECE_DIGITAL_MIN 			0
#define EARPIECE_DIGITAL_MAX 			127

/* Speaker levels */
#define SPEAKER_DEFAULT			12
#define SPEAKER_MIN 			0
#define SPEAKER_MAX 			18

#define SPEAKER_ANALOG_DEFAULT			0
#define SPEAKER_ANALOG_MIN 				0
#define SPEAKER_ANALOG_MAX 				6

#define SPEAKER_DIGITAL_DEFAULT			0
#define SPEAKER_DIGITAL_MIN 			0
#define SPEAKER_DIGITAL_MAX 			127

/* Both levels */
#define BOTH_DEFAULT			12
#define BOTH_MIN 				0
#define BOTH_MAX 				18

#define BOTH_ANALOG_DEFAULT				0
#define BOTH_ANALOG_MIN 				0
#define BOTH_ANALOG_MAX 				6

#define BOTH_DIGITAL_DEFAULT			0
#define BOTH_DIGITAL_MIN 				0
#define BOTH_DIGITAL_MAX 				127

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
#define EQ_B1_GAIN_DEFAULT		0
#define EQ_B2_GAIN_DEFAULT		0
#define EQ_B3_GAIN_DEFAULT		0
#define EQ_B4_GAIN_DEFAULT		0
#define EQ_B5_GAIN_DEFAULT		0

// Mixers
#define MADERA_MIXER_SOURCE_MASK	0xff
#define MADERA_MIXER_SOURCE_SHIFT	0
#define MADERA_MIXER_VOLUME_MASK	0xfe
#define MADERA_MIXER_VOLUME_SHIFT	1

// Mic
#define MIC_DEFAULT			0
#define MIC_DOWN_GAIN_DEFAULT		128
#define MIC_UP_GAIN_DEFAULT		128
#define MIC_HP_GAIN_DEFAULT		128


// REGS FOR GET AND SET
// Headphone
#define OUT2L_VOLUME \
	MADERA_DAC_DIGITAL_VOLUME_2L, \
	MADERA_OUT2L_VOL_MASK, \
	MADERA_OUT2L_VOL_SHIFT

#define OUT2R_VOLUME \
	MADERA_DAC_DIGITAL_VOLUME_2R, \
	MADERA_OUT1R_VOL_MASK, \
	MADERA_OUT1R_VOL_SHIFT

#define OUT2_MONO \
	MADERA_OUTPUT_PATH_CONFIG_2L, \
	MADERA_OUT2_MONO_MASK, \
	MADERA_OUT2_MONO_SHIFT

#define OUT2L_MIX \
	MADERA_OUT2LMIX_INPUT_1_SOURCE, \
	MADERA_MIXER_SOURCE_MASK, \
	MADERA_MIXER_SOURCE_SHIFT

#define OUT2R_MIX \
	MADERA_OUT2RMIX_INPUT_1_SOURCE, \
	MADERA_MIXER_SOURCE_MASK, \
	MADERA_MIXER_SOURCE_SHIFT

// Eq
#define EQ1_ENA \
	MADERA_EQ1_1, \
	MADERA_EQ1_ENA_MASK, \
	MADERA_EQ1_ENA_SHIFT

#define EQ2_ENA \
	MADERA_EQ2_1, \
	MADERA_EQ2_ENA_MASK, \
	MADERA_EQ2_ENA_SHIFT

#define EQ1_MIX \
	MADERA_EQ1MIX_INPUT_1_SOURCE, \
	MADERA_MIXER_SOURCE_MASK, \
	MADERA_MIXER_SOURCE_SHIFT

#define EQ2_MIX \
	MADERA_EQ2MIX_INPUT_1_SOURCE, \
	MADERA_MIXER_SOURCE_MASK, \
	MADERA_MIXER_SOURCE_SHIFT

// Mic
#define MIC1R_VOLUME \
	MADERA_ADC_DIGITAL_VOLUME_1R, \
	MADERA_IN1R_DIG_VOL_MASK, \
	MADERA_IN1R_DIG_VOL_SHIFT
	
#define MIC1L_VOLUME \
	MADERA_ADC_DIGITAL_VOLUME_1L, \
	MADERA_IN1L_DIG_VOL_MASK, \
	MADERA_IN1L_DIG_VOL_SHIFT

#define MIC2L_VOLUME \
	MADERA_ADC_DIGITAL_VOLUME_2L, \
	MADERA_IN2L_DIG_VOL_MASK, \
	MADERA_IN2L_DIG_VOL_SHIFT

#define MIC3L_VOLUME \
	MADERA_ADC_DIGITAL_VOLUME_3L, \
	MADERA_IN3L_DIG_VOL_MASK, \
	MADERA_IN3L_DIG_VOL_SHIFT
	
#define MIC4L_VOLUME \
	MADERA_ADC_DIGITAL_VOLUME_4L, \
	MADERA_IN4L_DIG_VOL_MASK, \
	MADERA_IN4L_DIG_VOL_SHIFT
