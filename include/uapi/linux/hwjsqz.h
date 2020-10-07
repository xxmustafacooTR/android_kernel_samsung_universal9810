/* include/uapi/linux/hwjsqz.h
 *
 * Userspace API for Samsung JSQZ driver
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jungik Seo <jungik.seo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __HWJSQZ_H__
#define __HWJSQZ_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

/*
 * NOTE: THESE ENUMS ARE ALSO USED BY USERSPACE TO SPECIFY SETTINGS AND THEY'RE
 *       SUPPOSED TO WORK WITH FUTURE HARDWARE AS WELL (WHICH MAY OR MAY NOT
 *       SUPPORT ALL THE SETTINGS.
 *
 *       FOR THAT REASON, YOU MUST NOT CHANGE THE ORDER OF VALUES,
 *       AND NEW VALUES MUST BE ADDED AT THE END OF THE ENUMS TO AVOID
 *       API BREAKAGES.
 */


/**
 * @brief The available jpeg squeezer modes
 */
enum hwJSQZ_processing_mode {
	JSQZ_MODE_NORMAL,			/**< normal mode */
	JSQZ_MODE_HIGH_QUALITY      /**< high quality mode */
};

/**
 * @brief The available jpeg squeezer functions
 */
enum hwJSQZ_processing_function {
	JSQZ_FUNCTION_LIVE,			/**< live mode - use preview of camera */
	JSQZ_FUNCTION_TRANSCODE	/**< transcode mode - use original image */
};

/**
 * @brief Available color space for the input image
 */
enum hwJSQZ_input_cs_format {
	JSQZ_INPUT_CS_FORMAT_NV21,		/**< NV21 format */
	JSQZ_INPUT_CS_FORMAT_YUV422	/**< YUV422 format */
};

/**
 * enum hwJSQZ_blk_size - The available block sizes
 *
 * @JSQZ_ENCODE_MODE_BLK_SIZE_4x4	: Block size 4x4
 * @JSQZ_ENCODE_MODE_BLK_SIZE_6x6	: Block size 6x6
 * @JSQZ_ENCODE_MODE_BLK_SIZE_8x8	: Block size 8x8
 */
enum hwJSQZ_blk_size {
	JSQZ_ENCODE_MODE_BLK_SIZE_4x4,
	JSQZ_ENCODE_MODE_BLK_SIZE_6x6,
	JSQZ_ENCODE_MODE_BLK_SIZE_8x8,
};

/**
 * enum hwJSQZ_input_pixel_format - Available pixel formats for the input image
 *
 * @JSQZ_ENCODE_INPUT_PIXEL_FORMAT_RGBA8888	: RGBA8888
 * @JSQZ_ENCODE_INPUT_PIXEL_FORMAT_ARGB8888	: ARGB8888
 * @JSQZ_ENCODE_INPUT_PIXEL_FORMAT_BGRA8888	: BGRA8888
 * @JSQZ_ENCODE_INPUT_PIXEL_FORMAT_ABGR8888	: ABGR8888
 */
enum hwJSQZ_input_pixel_format {
	JSQZ_ENCODE_INPUT_PIXEL_FORMAT_RGBA8888,
	JSQZ_ENCODE_INPUT_PIXEL_FORMAT_ARGB8888,
	JSQZ_ENCODE_INPUT_PIXEL_FORMAT_BGRA8888,
	JSQZ_ENCODE_INPUT_PIXEL_FORMAT_ABGR8888,
};

/**
 * @brief the image format and size
 */
struct hwJSQZ_img_info {
	__u32 fmt;
	__u32 width;					/**< width of the image */
	__u32 height;					/**< height of the image */
	__u32 stride;					/**<: stride of the image */
	enum hwJSQZ_input_cs_format cs;	/**< the image format. @sa hwJSQZ_input_cs_format */
};

/**
 * @brief the type of the buffer sent to the driver
 */
enum hwSQZ_buffer_type {
	HWSQZ_BUFFER_NONE,		/**< no buffer is set */
	HWSQZ_BUFFER_DMABUF,	/**< DMA buffer */
	HWSQZ_BUFFER_USERPTR	/**< a pointer to user memory */
};

/**
 * @brief the struct holding the description of a buffer
 *
 * A buffer can either be represented by a DMA file descriptor OR by a pointer
 * to user memory. The len must be initialized with the size of the buffer,
 * and the type must be set to the correct type.
 */
struct hwSQZ_buffer {
	union {
		__s32 fd;				/**< the DMA file descriptor, if the buffer is a DMA buffer. This is in a union with userptr, only one should be set. */
		unsigned long userptr;	/**< the ptr to user memory, if the buffer is in user memory. This is in a union with fd, only one should be set. */
	};
	size_t len;					/**< the declared buffer length, in bytes */
	__u8 type;					/**< the declared buffer type @sa hwJSQZ_buffer_type */
};

/**
 * @brief parameters for HW processing
 *
 * The fields needs to be explicitly set by userspace to the desired values,
 * using the corresponding enumerations.
 */
struct hwJSQZ_config {
	__u8       encodeBlockSize;
	__u8       intref_iterations;
	__u8       partitions;
	__u8       num_blk_mode;
	__u8       dual_plane_enable;

	/*
	 * Explicit padding to align struct to 64bit,
	 * This way the memory layout should be the same on both 32 and 64bit,
	 * and will avoid the need for a 32-bit specific structure
	 * for compat_ioctl
	 */
	__u8       reserved[3];
	enum hwJSQZ_processing_mode		mode;		/**< mode of Jpeg Squeezer */
	enum hwJSQZ_processing_function	function;	/**< function of Jpeg Squeezer */
};

/**
 * @brief the task information to be sent to the driver
 *
 * This is the struct holding all the task information that the driver needs
 * to process a task.
 *
 * The client needs to fill it in with the necessary information and send it
 * to the driver through the IOCTL.
 */
struct hwJSQZ_task {
	struct hwJSQZ_img_info info_out;    /**< info related to the image sent to the HW for processing */
	struct hwSQZ_buffer buf_out[2];    /**< info related to the buffer sent to the HW for processing */
	struct hwJSQZ_config config;        /**< the configuration for HW IP */
	struct hwJSQZ_img_info info_cap;
	struct hwJSQZ_config enc_config;
	unsigned int buf_q[32];             /**< q table that are result of jpeg squeezer */
	unsigned int buf_init_q[32];        /**< q table that are initial Q */
	int num_of_buf;                     /**< number of buf_out count */
	/* private: internal use only */
	unsigned long reserved[2];
};


#define HWJSQZ_IOC_PROCESS	_IOWR('M', 0, struct hwJSQZ_task)


#endif // __HWJSQZ_H__

