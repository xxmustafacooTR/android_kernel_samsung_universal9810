/*
 * moro_sound.c  --  Sound mod for Madera/CS47L92, S9 sound driver
 *
 * Author	: @morogoku https://github.com/morogoku
 * 
 * Date		: March 2019 - v2.0
 *		: April 2019 - v2.1
 *
 *
 * Based on the Boeffla Sound 1.6 for Galaxy S3
 *
 * Credits: 	andip71, author of Boeffla Sound
 *		Supercurio, Yank555 and Gokhanmoral.
 *
 *		AndreiLux, for his Madera control sound mod
 *		
 *		Flar2, for his speaker gain mod
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "moro_sound.h"


/*****************************************/
// Variables
/*****************************************/

// pointer to regmap
static struct regmap *map;

// internal moro sound variables
static int first = 1;				// first access

static int moro_sound;				// moro sound master switch
static int debug;				// debug switch

static int headphone_gain_l;			// headphone volume left
static int headphone_gain_r;			// headphone volume right

static int earpiece_gain;			// earpiece volume
static int speaker_gain;			// speaker volume

static int out2l_mix_source;			// out2 mix source left
static int out2r_mix_source;			// out2 mix source right
static int eq1_mix_source;			// eq1 mix source left
static int eq2_mix_source;			// eq2 mix soirce right

static int eq;					// eq master switch
static int eq_gains[5] = {EQ_GAIN_DEFAULT};	// eq 5 bands gains


/*****************************************/
// Internal function declarations
/*****************************************/

static unsigned int get_headphone_gain_l(void);
static unsigned int get_headphone_gain_r(void);
static void set_headphone_gain_l(int gain);
static void set_headphone_gain_r(int gain);

static void set_out2l_mix_source(int value);
static void set_out2r_mix_source(int value);

static void set_eq1_mix_source(int value);
static void set_eq2_mix_source(int value);

static void set_eq(void);
static void set_eq_gains(void);

static void reset_moro_sound(void);
static void reset_audio_hub(void);
static void update_audio_hub(void);


/*****************************************/
// Internal helper functions
/*****************************************/

#define _write(reg, val) _regmap_write_nohook(map, reg, val)

#define _read(reg, val) regmap_read(map, reg, val)

static unsigned int get_headphone_gain_l(void)
{
	unsigned int val;

	_read(MADERA_DAC_DIGITAL_VOLUME_2L, &val);
	val &= MADERA_OUT2L_VOL_MASK;
	val >>= MADERA_OUT2L_VOL_SHIFT;

	return val;
}

static void set_headphone_gain_l(int gain)
{
	unsigned int val;

	_read(MADERA_DAC_DIGITAL_VOLUME_2L, &val);
	val &= ~MADERA_OUT2L_VOL_MASK;
	val |= (gain << MADERA_OUT2L_VOL_SHIFT);
	_write(MADERA_DAC_DIGITAL_VOLUME_2L, val);
}

static unsigned int get_headphone_gain_r(void)
{
	unsigned int val;

	_read(MADERA_DAC_DIGITAL_VOLUME_2R, &val);
	val &= MADERA_OUT2R_VOL_MASK;
	val >>= MADERA_OUT2R_VOL_SHIFT;

	return val;
}

static void set_headphone_gain_r(int gain)
{
	unsigned int val;

	_read(MADERA_DAC_DIGITAL_VOLUME_2R, &val);
	val &= ~MADERA_OUT2R_VOL_MASK;
	val |= (gain << MADERA_OUT2R_VOL_SHIFT);
	_write(MADERA_DAC_DIGITAL_VOLUME_2R, val);
}

static void set_out2l_mix_source(int value)
{
	unsigned int val;

	_read(MADERA_OUT2LMIX_INPUT_1_SOURCE, &val);
	val &= ~MADERA_MIXER_SOURCE_MASK;
	val |= (value << MADERA_MIXER_SOURCE_SHIFT);
	_write(MADERA_OUT2LMIX_INPUT_1_SOURCE, val);
}

static void set_out2r_mix_source(int value)
{
	unsigned int val;

	_read(MADERA_OUT2RMIX_INPUT_1_SOURCE, &val);
	val &= ~MADERA_MIXER_SOURCE_MASK;
	val |= (value << MADERA_MIXER_SOURCE_SHIFT);
	_write(MADERA_OUT2RMIX_INPUT_1_SOURCE, val);
}

static void set_eq1_mix_source(int value)
{
	unsigned int val;

	_read(MADERA_EQ1MIX_INPUT_1_SOURCE, &val);
	val &= ~MADERA_MIXER_SOURCE_MASK;
	val |= (value << MADERA_MIXER_SOURCE_SHIFT);
	_write(MADERA_EQ1MIX_INPUT_1_SOURCE, val);
}


static void set_eq2_mix_source(int value)
{
	unsigned int val;

	_read(MADERA_EQ2MIX_INPUT_1_SOURCE, &val);
	val &= ~MADERA_MIXER_SOURCE_MASK;
	val |= (value << MADERA_MIXER_SOURCE_SHIFT);
	_write(MADERA_EQ2MIX_INPUT_1_SOURCE, val);
}

static void set_eq(void)
{
	unsigned int val;

	// If EQ is enabled
	if (eq & moro_sound)
	{
		// Enable EQ1 for left channel
		_read(MADERA_EQ1_1, &val);
		val &= ~MADERA_EQ1_ENA_MASK;
		val |= 1 << MADERA_EQ1_ENA_SHIFT;
		_write(MADERA_EQ1_1, val);

		// Enable EQ2 for right channel
		_read(MADERA_EQ2_1, &val);
		val &= ~MADERA_EQ2_ENA_MASK;
		val |= 1 << MADERA_EQ2_ENA_SHIFT;
		_write(MADERA_EQ2_1, val);

		// Set mixers
		eq1_mix_source = 33;	// EQ1 -> AIF1 RX1 left
		eq2_mix_source = 33;	// EQ2 -> AIF1 RX2 right
		set_eq1_mix_source(eq1_mix_source);
		set_eq2_mix_source(eq2_mix_source);

		out2l_mix_source = 81;	// OUT2L -> EQ1 left
		out2r_mix_source = 81;	// OUT2R -> EQ2 right
		set_out2l_mix_source(out2l_mix_source);
		set_out2r_mix_source(out2r_mix_source);
	}
	// If EQ is disabled
	else
	{
		// Disable EQ1
		_read(MADERA_EQ1_1, &val);
		val &= ~MADERA_EQ1_ENA_MASK;
		val |= 0 << MADERA_EQ1_ENA_SHIFT;
		_write(MADERA_EQ1_1, val);

		// Disable EQ2
		_read(MADERA_EQ2_1, &val);
		val &= ~MADERA_EQ2_ENA_MASK;
		val |= 0 << MADERA_EQ2_ENA_SHIFT;
		_write(MADERA_EQ2_1, val);

		// Set mixers to default
		eq1_mix_source = EQ1_MIX_DEFAULT;
		eq2_mix_source = EQ2_MIX_DEFAULT;
		set_eq1_mix_source(eq1_mix_source);
		set_eq2_mix_source(eq2_mix_source);

		out2l_mix_source = OUT2L_MIX_DEFAULT;
		out2r_mix_source = OUT2R_MIX_DEFAULT;
		set_out2l_mix_source(out2l_mix_source);
		set_out2r_mix_source(out2r_mix_source);
	}

	set_eq_gains();
}

static void set_eq_gains(void)
{
	unsigned int val;
	unsigned int gain1, gain2, gain3, gain4, gain5;

	gain1 = eq_gains[0];
	gain2 = eq_gains[1];
	gain3 = eq_gains[2];
	gain4 = eq_gains[3];
	gain5 = eq_gains[4];

	// First register
	// read current value from audio hub and mask all bits apart from equalizer enabled bit,
	// add individual gains and write back to audio hub
	_read(MADERA_EQ1_1, &val);
	val &= MADERA_EQ1_ENA_MASK;
	val |= ((gain1 + EQ_GAIN_OFFSET) << MADERA_EQ1_B1_GAIN_SHIFT);
	val |= ((gain2 + EQ_GAIN_OFFSET) << MADERA_EQ1_B2_GAIN_SHIFT);
	val |= ((gain3 + EQ_GAIN_OFFSET) << MADERA_EQ1_B3_GAIN_SHIFT);
	_write(MADERA_EQ1_1, val);
	_write(MADERA_EQ2_1, val);

	// second register
	// read current value from audio hub and mask all bits apart from band1 mode bit,
	// set individual gains and write back to audio hub
	_read(MADERA_EQ1_2, &val);
	val &= MADERA_EQ1_B1_MODE_MASK;
	val |= ((gain4 + EQ_GAIN_OFFSET) << MADERA_EQ1_B4_GAIN_SHIFT);
	val |= ((gain5 + EQ_GAIN_OFFSET) << MADERA_EQ1_B5_GAIN_SHIFT);
	_write(MADERA_EQ1_2, val);
	_write(MADERA_EQ2_2, val);

	if (debug)
		printk("Moro-sound: written the new EQ gain values\n");
}


/*****************************************/
// Sound hook functions
/*****************************************/

void moro_sound_hook_madera_pcm_probe(struct regmap *pmap)
{
	// store a copy of the pointer to the regmap, we need
	// that for internal calls to the audio hub
	map = pmap;

	// Print debug info
	printk("Moro-sound: regmap pointer received\n");

	// Initialize moro sound master switch finally
	moro_sound = MORO_SOUND_DEFAULT;
	eq = EQ_DEFAULT;
	set_eq();

	// If moro sound is enabled during driver start, reset to default configuration
	if (moro_sound)
	{
		reset_moro_sound();
		printk("Moro-sound: moro sound enabled during startup\n");
	}
}

unsigned int moro_sound_write_hook(unsigned int reg, unsigned int val)
{
	// if moro sound is off, return original value
	if (!moro_sound)
		return val;

	// based on the register, do the appropriate processing
	switch (reg)
	{
		// headphone l
		case MADERA_DAC_DIGITAL_VOLUME_2L:
		{
			val &= ~MADERA_OUT2L_VOL_MASK;
			val |= (headphone_gain_l << MADERA_OUT2L_VOL_SHIFT);
			break;
		}

		// headphone r
		case MADERA_DAC_DIGITAL_VOLUME_2R:
		{
			val &= ~MADERA_OUT2R_VOL_MASK;
			val |= (headphone_gain_r << MADERA_OUT2R_VOL_SHIFT);
			break;
		}

		// earpiece
		case MADERA_DAC_DIGITAL_VOLUME_3L:
		{
			val &= ~MADERA_OUT3L_VOL_MASK;
			val |= (earpiece_gain << MADERA_OUT3L_VOL_SHIFT);
			break;
		}

		if (eq){
			// hpout2 l
			case MADERA_OUT2LMIX_INPUT_1_SOURCE:
			{
				val &= ~MADERA_MIXER_SOURCE_MASK;
				val |= (out2l_mix_source << MADERA_MIXER_SOURCE_SHIFT);
				break;
			}
			// hpout2 r
			case MADERA_OUT2RMIX_INPUT_1_SOURCE:
			{
				val &= ~MADERA_MIXER_SOURCE_MASK;
				val |= (out2r_mix_source << MADERA_MIXER_SOURCE_SHIFT);
				break;
			}
		}
	}

	return val;
}


/*****************************************/
// Initialization functions
/*****************************************/

static void reset_moro_sound(void)
{
	// set all moro sound config settings to defaults

	earpiece_gain = EARPIECE_DEFAULT;
	speaker_gain = SPEAKER_DEFAULT;

	headphone_gain_l = HEADPHONE_DEFAULT;
	headphone_gain_r = HEADPHONE_DEFAULT;

	out2l_mix_source = OUT2L_MIX_DEFAULT;
	out2r_mix_source = OUT2R_MIX_DEFAULT;

	eq1_mix_source = EQ1_MIX_DEFAULT;
	eq2_mix_source = EQ2_MIX_DEFAULT;

	debug = DEBUG_DEFAULT;
	
	if (debug)
		printk("Moro-sound: moro sound reset done\n");
}


static void reset_audio_hub(void)
{
	// reset all audio hub registers back to defaults

	set_headphone_gain_l(HEADPHONE_DEFAULT);
	set_headphone_gain_r(HEADPHONE_DEFAULT);

	set_earpiece_gain_value(EARPIECE_DEFAULT);
	set_speaker_gain_value(SPEAKER_DEFAULT);

	set_out2l_mix_source(OUT2L_MIX_DEFAULT);
	set_out2r_mix_source(OUT2R_MIX_DEFAULT);

	set_eq1_mix_source(EQ1_MIX_DEFAULT);
	set_eq2_mix_source(EQ2_MIX_DEFAULT);

	set_eq();
	
	if (debug)
		printk("Moro-sound: madera audio hub reset done\n");
}


static void update_audio_hub(void)
{
	// reset all audio hub registers back to defaults

	set_earpiece_gain_value(earpiece_gain);
	set_speaker_gain_value(speaker_gain);

	set_headphone_gain_l(headphone_gain_l);
	set_headphone_gain_r(headphone_gain_r);

	set_out2l_mix_source(out2l_mix_source);
	set_out2r_mix_source(out2r_mix_source);

	set_eq1_mix_source(eq1_mix_source);
	set_eq2_mix_source(eq2_mix_source);

	set_eq();

	if (debug)
		printk("Moro-sound: madera audio hub updated done\n");
}


/*****************************************/
// sysfs interface functions
/*****************************************/

// Moro sound master switch

static ssize_t moro_sound_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// print current value
	return sprintf(buf, "%d\n", moro_sound);
}


static ssize_t moro_sound_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if (((val == 0) || (val == 1)))
	{
		// check if there was a change
		if (moro_sound != val)
		{
			// set new status
			moro_sound = val;

			// re-initialize settings and audio hub (in any case for both on and off !)
			// if is the first enable, reset variables
			if(first) {
				reset_moro_sound();
				first = 0;
			}

			if(val == 1) update_audio_hub();
			if(val == 0) reset_audio_hub();
		}

		// print debug info
		if (debug)
			printk("Moro-sound: status %d\n", moro_sound);
	}

	return count;
}


// Headphone volume

static ssize_t headphone_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// print current values
	return sprintf(buf, "%d %d\n", headphone_gain_l, headphone_gain_r);
}


static ssize_t headphone_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val_l;
	int val_r;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d %d", &val_l, &val_r);

	if (ret != 2)
		return -EINVAL;

	if (val_l < HEADPHONE_MIN)
		val_l = HEADPHONE_MIN;
	if (val_l > HEADPHONE_MAX)
		val_l = HEADPHONE_MAX;
	if (val_r < HEADPHONE_MIN)
		val_r = HEADPHONE_MIN;
	if (val_r > HEADPHONE_MAX)
		val_r = HEADPHONE_MAX;

	// store new values
	headphone_gain_l = val_l;
	headphone_gain_r = val_r;

	// set new values
	set_headphone_gain_l(headphone_gain_l);
	set_headphone_gain_r(headphone_gain_r);

	// print debug info
	if (debug)
		printk("Moro-sound: headphone volume L=%d R=%d\n", headphone_gain_l, headphone_gain_r);

	return count;
}
static ssize_t headphone_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return version information
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", HEADPHONE_MIN, HEADPHONE_MAX, HEADPHONE_DEFAULT);
}
// Earpiece Volume
static ssize_t earpiece_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// print current values
	return sprintf(buf, "%d\n", earpiece_gain);
}
static ssize_t earpiece_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;
	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;
	// read values from input buffer
	ret = sscanf(buf, "%d", &val);
	if (ret != 1)
		return -EINVAL;
	if (val < EARPIECE_MIN)
		val = EARPIECE_MIN;
	if (val > EARPIECE_MAX)
		val = EARPIECE_MAX;
	// store new values
	earpiece_gain = val;
	// set new values
	set_earpiece_gain_value(earpiece_gain);
	// print debug info
	if (debug)
		printk("Moro-sound: earpiece volume: %d\n", earpiece_gain);
	return count;
}
static ssize_t earpiece_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return version information
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", EARPIECE_MIN, EARPIECE_MAX, EARPIECE_DEFAULT);
}
// Speaker Volume
static ssize_t speaker_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// print current values
	return sprintf(buf, "%d\n", speaker_gain);
}
static ssize_t speaker_gain_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;
	// read values from input buffer
	ret = sscanf(buf, "%d", &val);
	if (ret != 1)
		return -EINVAL;
	if (val < SPEAKER_MIN)
		val = SPEAKER_MIN;
	if (val > SPEAKER_MAX)
		val = SPEAKER_MAX;
	// store new values
	speaker_gain = val;
	// set new values
	set_speaker_gain_value(speaker_gain);
	// print debug info
	if (debug)
		printk("Moro-sound: speaker volume: %d\n", speaker_gain);
	return count;
}
static ssize_t speaker_limits_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return version information
	return sprintf(buf, "Min:%u Max:%u Def:%u\n", SPEAKER_MIN, SPEAKER_MAX, SPEAKER_DEFAULT);
}


// EQ

static ssize_t eq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	
	// print current value
	return sprintf(buf, "%d\n", eq);
}

static ssize_t eq_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	// store if valid data
	if (((val == 0) || (val == 1)))
	{
		// check if there was a change
		if (eq != val)
		{
			// store new value
			eq = val;

			set_eq();
		}

		// print debug info
		if (debug)
			printk("Moro-sound: EQ status: %d\n", eq);
	}

	return count;
}


// EQ GAIN

static ssize_t eq_gains_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// print current values
	return sprintf(buf, "%d %d %d %d %d\n", eq_gains[0], eq_gains[1], eq_gains[2], eq_gains[3], eq_gains[4]);
}

static ssize_t eq_gains_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int gains[5];
	int i;

	// Terminate if moro sound is not enabled
	if (!moro_sound)
		return count;

	// read values from input buffer
	ret = sscanf(buf, "%d %d %d %d %d", &gains[0], &gains[1], &gains[2], &gains[3], &gains[4]);

	if (ret != 5)
		return -EINVAL;

	// check validity of gain values and adjust
	for (i = 0; i <= 4; i++)
	{
		if (gains[i] < EQ_GAIN_MIN)
			gains[i] = EQ_GAIN_MIN;

		if (gains[i] > EQ_GAIN_MAX)
			gains[i] = EQ_GAIN_MAX;

		eq_gains[i] = gains[i];
	}

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ gains: %d %d %d %d %d\n", eq_gains[0], eq_gains[1], eq_gains[2], eq_gains[3], eq_gains[4]);

	return count;
}

static ssize_t eq_b1_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", eq_gains[0]);
}

static ssize_t eq_b1_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[0] = val;

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ Band1 gain: %d\n", eq_gains[0]);

	return count;
}

static ssize_t eq_b2_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", eq_gains[1]);
}

static ssize_t eq_b2_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[1] = val;

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ Band2 gain: %d\n", eq_gains[1]);

	return count;
}

static ssize_t eq_b3_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", eq_gains[2]);
}

static ssize_t eq_b3_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[2] = val;

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ Band3 gain: %d\n", eq_gains[2]);

	return count;
}

static ssize_t eq_b4_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", eq_gains[3]);
}

static ssize_t eq_b4_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;

	// store new value
	eq_gains[3] = val;

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ Band4 gain: %d\n", eq_gains[3]);

	return count;
}

static ssize_t eq_b5_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", eq_gains[4]);
}

static ssize_t eq_b5_gain_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if (val < EQ_GAIN_MIN)
		val = EQ_GAIN_MIN;

	if (val > EQ_GAIN_MAX)
		val = EQ_GAIN_MAX;
	
	// store new value
	eq_gains[4] = val;

	// set new values
	set_eq_gains();

	// print debug info
	if (debug)
		printk("Moro-sound: EQ Band5 gain: %d\n", eq_gains[4]);

	return count;
}


// Debug status

static ssize_t debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return current debug status
	return sprintf(buf, "%d\n", debug);
}

static ssize_t debug_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int ret = -EINVAL;
	unsigned int val;

	// check data and store if valid
	ret = sscanf(buf, "%d", &val);

	if (ret != 1)
		return -EINVAL;

	if ((val == 0) || (val == 1))
		debug = val;

	return count;
}


// Register dump

static ssize_t reg_dump_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int out2_ena, out2l_mix, out2r_mix, eq1_ena, eq2_ena, eq1_mix, eq2_mix, eq_b1,
			eq_b2, eq_b3, eq_b4, eq_b5;

	_read(MADERA_OUTPUT_ENABLES_1, &out2_ena);
		out2_ena = (out2_ena & MADERA_OUT2L_ENA_MASK) >> MADERA_OUT2L_ENA_SHIFT;

	_read(MADERA_OUT2LMIX_INPUT_1_SOURCE, &out2l_mix);

	_read(MADERA_OUT2RMIX_INPUT_1_SOURCE, &out2r_mix);

	_read(MADERA_EQ1_1, &eq1_ena);
		eq1_ena = (eq1_ena & MADERA_EQ1_ENA_MASK) >> MADERA_EQ1_ENA_SHIFT;

	_read(MADERA_EQ2_1, &eq2_ena);
		eq2_ena = (eq2_ena & MADERA_EQ2_ENA_MASK) >> MADERA_EQ2_ENA_SHIFT;

	_read(MADERA_EQ1MIX_INPUT_1_SOURCE, &eq1_mix);

	_read(MADERA_EQ2MIX_INPUT_1_SOURCE, &eq2_mix);

	_read(MADERA_EQ1_1, &eq_b1);
		eq_b1 = ((eq_b1 & MADERA_EQ1_B1_GAIN_MASK) >> MADERA_EQ1_B1_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	_read(MADERA_EQ1_1, &eq_b2);
		eq_b2 = ((eq_b2 & MADERA_EQ1_B2_GAIN_MASK) >> MADERA_EQ1_B2_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	_read(MADERA_EQ1_1, &eq_b3);
		eq_b3 = ((eq_b3 & MADERA_EQ1_B3_GAIN_MASK) >> MADERA_EQ1_B3_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	_read(MADERA_EQ1_2, &eq_b4);
		eq_b4 = ((eq_b4 & MADERA_EQ1_B4_GAIN_MASK) >> MADERA_EQ1_B4_GAIN_SHIFT) - EQ_GAIN_OFFSET;
	_read(MADERA_EQ1_2, &eq_b5);
		eq_b5 = ((eq_b5 & MADERA_EQ1_B5_GAIN_MASK) >> MADERA_EQ1_B5_GAIN_SHIFT) - EQ_GAIN_OFFSET;

	
	
	
	// return register dump information
	return sprintf(buf, "\
headphone_gain_l: reg: %d, variable: %d\n\
headphone_gain_r: reg: %d, variable: %d\n\
first enable: %d\n\
earpiece_gain: %d\n\
speaker_gain: %d\n\
HPOUT2 Enabled: %d\n\
HPOUT2L Source: %d\n\
HPOUT2R Source: %d\n\
EQ1 Enabled: %d\n\
EQ2 Enabled: %d\n\
EQ1MIX source: %d\n\
EQ2MIX source: %d\n\
EQ b1 gain: %d\n\
EQ b2 gain: %d\n\
EQ b3 gain: %d\n\
EQ b4 gain: %d\n\
EQ b5 gain: %d\n\
", 
get_headphone_gain_l(),
headphone_gain_l,
get_headphone_gain_r(),
headphone_gain_r,
first,
get_earpiece_gain(),
get_speaker_gain(),
out2_ena,
out2l_mix,
out2r_mix,
eq1_ena,
eq2_ena,
eq1_mix,
eq2_mix,
eq_b1,
eq_b2,
eq_b3,
eq_b4,
eq_b5);
}


// Version information

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	// return version information
	return sprintf(buf, "%s\n", MORO_SOUND_VERSION);
}


/*****************************************/
// Initialize moro sound sysfs folder
/*****************************************/

// define objects
static DEVICE_ATTR(moro_sound, 0664, moro_sound_show, moro_sound_store);
static DEVICE_ATTR(headphone_gain, 0664, headphone_gain_show, headphone_gain_store);
static DEVICE_ATTR(headphone_limits, 0664, headphone_limits_show, NULL);
static DEVICE_ATTR(earpiece_gain, 0664, earpiece_gain_show, earpiece_gain_store);
static DEVICE_ATTR(earpiece_limits, 0664, earpiece_limits_show, NULL);
static DEVICE_ATTR(speaker_gain, 0664, speaker_gain_show, speaker_gain_store);
static DEVICE_ATTR(speaker_limits, 0664, speaker_limits_show, NULL);
static DEVICE_ATTR(eq, 0664, eq_show, eq_store);
static DEVICE_ATTR(eq_gains, 0664, eq_gains_show, eq_gains_store);
static DEVICE_ATTR(eq_b1_gain, 0664, eq_b1_gain_show, eq_b1_gain_store);
static DEVICE_ATTR(eq_b2_gain, 0664, eq_b2_gain_show, eq_b2_gain_store);
static DEVICE_ATTR(eq_b3_gain, 0664, eq_b3_gain_show, eq_b3_gain_store);
static DEVICE_ATTR(eq_b4_gain, 0664, eq_b4_gain_show, eq_b4_gain_store);
static DEVICE_ATTR(eq_b5_gain, 0664, eq_b5_gain_show, eq_b5_gain_store);
static DEVICE_ATTR(debug, 0664, debug_show, debug_store);
static DEVICE_ATTR(version, 0664, version_show, NULL);
static DEVICE_ATTR(reg_dump, 0664, reg_dump_show, NULL);

// define attributes
static struct attribute *moro_sound_attributes[] = {
	&dev_attr_moro_sound.attr,
	&dev_attr_headphone_gain.attr,
	&dev_attr_headphone_limits.attr,
	&dev_attr_earpiece_gain.attr,
	&dev_attr_earpiece_limits.attr,
	&dev_attr_speaker_gain.attr,
	&dev_attr_speaker_limits.attr,
	&dev_attr_eq.attr,
	&dev_attr_eq_gains.attr,
	&dev_attr_eq_b1_gain.attr,
	&dev_attr_eq_b2_gain.attr,
	&dev_attr_eq_b3_gain.attr,
	&dev_attr_eq_b4_gain.attr,
	&dev_attr_eq_b5_gain.attr,
	&dev_attr_debug.attr,
	&dev_attr_version.attr,
	&dev_attr_reg_dump.attr,
	NULL
};

// define attribute group
static struct attribute_group moro_sound_control_group = {
	.attrs = moro_sound_attributes,
};


// define control device
static struct miscdevice moro_sound_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "moro_sound",
};


/*****************************************/
// Driver init and exit functions
/*****************************************/

static int __init moro_sound_init(void)
{
	int err = 0;

	// register moro sound control device
	err = misc_register(&moro_sound_control_device);
	if (err) {
		pr_err("failed register the device.\n");
		return err;
	}

	err = sysfs_create_group(&moro_sound_control_device.this_device->kobj,
				 &moro_sound_control_group);
	if (err) {
		pr_err("failed to create sys fs object.\n");
		misc_deregister(&moro_sound_control_device);
		return err;
	}

	// Initialize moro sound master switch with OFF per default (will be set to correct
	// default value when we receive the codec pointer later - avoids startup boot loop)
	moro_sound = 0;
	eq = 0;

	// Initialize variables
	reset_moro_sound();

	// Print debug info
	printk("Moro-sound: engine version %s started\n", MORO_SOUND_VERSION);

	return 0;
}


static void __exit moro_sound_exit(void)
{
	// remove moro sound control device
	sysfs_remove_group(&moro_sound_control_device.this_device->kobj,
                           &moro_sound_control_group);

	misc_deregister(&moro_sound_control_device);

	// Print debug info
	printk("Moro-sound: engine stopped\n");
}


/* define driver entry points */

module_init(moro_sound_init);
module_exit(moro_sound_exit);

