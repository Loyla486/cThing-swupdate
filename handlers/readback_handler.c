/*
 * SPDX-FileCopyrightText: 2020 Bosch Sicherheitssysteme GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "swupdate.h"
#include "handler.h"    
#include "util.h"

void readfront_handler(void);
void readback_handler(void);
static int verify(struct img_type *img, const char* handler_name);

static int readfront(struct img_type *img, void *data) {
    if (!data)
		return -1;

	script_fn scriptfn = *(script_fn *)data;
	switch (scriptfn) {
        case PREINSTALL:
            INFO("Entering readfront handler");
            return verify(img, "readfront");
        case POSTINSTALL:
        default:
            return 0;
	}
}
static int readback(struct img_type *img, void *data) {
    if (!data)
		return -1;

	script_fn scriptfn = *(script_fn *)data;
	switch (scriptfn) {
        case POSTINSTALL:
            INFO("Entering readback handler");
            return verify(img, "readback");
        case PREINSTALL:
        default:
            return 0;
	}
}


static int verify(struct img_type *img, const char* handler_name)
{
	/* Get property: partition hash */
	unsigned char hash[SHA256_HASH_LENGTH];
	char *ascii_hash = dict_get_value(&img->properties, "sha256");
	if (!ascii_hash || ascii_to_hash(hash, ascii_hash) < 0 || !IsValidHash(hash)) {
		ERROR("Invalid hash");
		return -EINVAL;
	}

	/* Get property: partition size */
	unsigned int size = 0;
	char *value = dict_get_value(&img->properties, "size");
	if (value) {
		size = strtoul(value, NULL, 10);
	} else {
		TRACE("Property size not found, use partition size");
	}

	/* Get property: offset */
	unsigned long offset = 0;
	value = dict_get_value(&img->properties, "offset");
	if (value) {
		offset = strtoul(value, NULL, 10);
	} else {
		TRACE("Property offset not found, use default 0");
	}

	/* Open the device (partition) */
	int fdin = open(img->device, O_RDONLY);
	if (fdin < 0) {
		ERROR("Failed to open %s: %s", img->device, strerror(errno));
		return -ENODEV;
	}

	/* Get the real size of the partition, if size is not set. */
	if (size == 0) {
		if (ioctl(fdin, BLKGETSIZE64, &size) < 0) {
			ERROR("Cannot get size of %s", img->device);
			close(fdin);
			return -EFAULT;
		}
		TRACE("Partition size: %u", size);
	}

	/* 
	 * Seek the file descriptor before passing it to copyfile().
	 * This is necessary because copyfile() only accepts streams,
	 * so the file descriptor shall be already at the right position.
	 */
	if (lseek(fdin, offset, SEEK_SET) < 0) {
		ERROR("Seek %lu bytes failed: %s", offset, strerror(errno));
		close(fdin);
		return -EFAULT;
	}

	/*
	 * Perform hash verification. We do not need to pass an output device to
	 * the copyfile() function, because we only want it to verify the hash of
	 * the input device.
	 */
	unsigned long offset_out = 0;
	int status = copyfile(fdin,
			NULL,  /* no output */
			size,
			&offset_out,
			0,     /* no output seek */
			1,     /* skip file, do not write to the output */
			0,     /* no compressed */
			NULL,  /* no checksum */
			hash,
			0,     /* no encrypted */
			NULL); /* no callback */
	if (status == 0) {
		INFO("%s verification success %s", handler_name, ascii_hash);
	} else {
		ERROR("%s verification failed, status=%d", handler_name, status);
	}

	close(fdin);
	return status;
}

__attribute__((constructor))
void readback_handler(void)
{
	register_handler("readback", readback, SCRIPT_HANDLER | NO_DATA_HANDLER, NULL);
}

__attribute__((constructor))
void readfront_handler(void)
{
	register_handler("readfront", readfront, SCRIPT_HANDLER | NO_DATA_HANDLER, NULL);
}
