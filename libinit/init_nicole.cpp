/*
   SPDX-FileCopyrightText: 2007 The Android Open Source Project
   SPDX-FileCopyrightText: 2016 The CyanogenMod Project
   SPDX-FileCopyrightText: The LineageOS Project
   SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mount.h>
#include <linux/gpio.h>
#include <algorithm>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "vendor_init.h"
#include "property_service.h"
#include "util.h"

using android::base::ReadFileToString;
using android::base::StartsWith;
using android::base::Trim;
using android::init::IsRecoveryMode;
using android::init::kRestoreconProperty;
using android::init::ReadFile;

#define GPIO_VARIANT_ID_0	3
#define GPIO_VARIANT_ID_1	6
#define GPIO_VARIANT_ID_2	7
#define GPIO_BOARD_REV_0	45
#define GPIO_BOARD_REV_1	50
#define GPIO_BOARD_REV_2	51

void property_override(char const prop[], char const value[], bool add = true)
{
    auto pi = (prop_info *) __system_property_find(prop);

    if (pi != nullptr) {
        __system_property_update(pi, value, strlen(value));
    } else if (add) {
        __system_property_add(prop, strlen(prop), value, strlen(value));
    }
}

static void set_serial()
{
	std::string sn;

	if (ReadFileToString("/mnt/vendor/persist/.sn.bin", &sn)) {
		/*
		 * The first 32 bytes are the "pcb sn".
		 * The second 32 bytes are the "ad sn", used as serialno.
		 * The rest of the file is unused.
		 * On prototypes the "ad sn" is empty. In this case we fall back to pcb sn.
		 */
		if (sn.size() > 32 && strlen(sn.substr(32).c_str()) > 0)
			sn = sn.substr(32);
		sn.resize(std::min((size_t) 32, strlen(sn.c_str())));
		if (sn.empty())
			sn = "0000000";
	} else {
        LOG(ERROR) << "Unable to read serial number";
		sn = "0000000";
	}

	property_override("ro.serialno", sn.c_str());
}

static int get_board_rev()
{
	int fd = open("/dev/gpiochip0", O_WRONLY);

	struct gpiohandle_request req;
	memset(&req, 0, sizeof(req));

	req.lineoffsets[0] = GPIO_BOARD_REV_0;
	req.lineoffsets[1] = GPIO_BOARD_REV_1;
	req.lineoffsets[2] = GPIO_BOARD_REV_2;
	req.flags = GPIOHANDLE_REQUEST_INPUT;
	strcpy(req.consumer_label, "board_rev");
	req.lines = 3;

	int ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret == -1)
		ret = -errno;

	close(fd);

	if (ret < 0) {
		LOG(ERROR) << "Failed to issue GPIO_GET_LINEHANDLE_IOCTL"
			<< ret << " " << strerror(errno);
		return ret;
	}

	gpiohandle_data data;
	ret = ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
	if (ret == -1)
		ret = -errno;

	if (ret < 0) {
		LOG(ERROR) << "Failed to issue GPIOHANDLE_GET_LINE_VALUES_IOCTL"
			<< ret << " " << strerror(errno);
		close(req.fd);
		return ret;
	}

	close(req.fd);

	return data.values[0] | (data.values[1] << 1) | (data.values[2] << 2);
}

static void set_board_rev()
{
	std::string board_rev = "UNKNOWN";

	switch (get_board_rev())
	{
	case 0: board_rev = "EVB"; break;
	case 1: board_rev = "T0"; break;
	case 2: board_rev = "EVT1"; break;
	case 3: board_rev = "EVT2"; break;
	case 4: board_rev = "DVT1"; break;
	case 5: board_rev = "DVT2"; break;
	case 6: board_rev = "DVT3"; break;
	case 7: board_rev = "MP"; break; // There was no change from PVT to MP
	}

	property_override("ro.product.board_rev", board_rev.c_str());
}

static int get_variant_id()
{
	int fd = open("/dev/gpiochip0", O_WRONLY);

	struct gpiohandle_request req;
	memset(&req, 0, sizeof(req));

	req.lineoffsets[0] = GPIO_VARIANT_ID_0;
	req.lineoffsets[1] = GPIO_VARIANT_ID_1;
	req.lineoffsets[2] = GPIO_VARIANT_ID_2;
	req.flags = GPIOHANDLE_REQUEST_INPUT;
	strcpy(req.consumer_label, "variant_id");
	req.lines = 3;

	int ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret == -1)
		ret = -errno;

	close(fd);

	if (ret < 0) {
		LOG(ERROR) << "Failed to issue GPIO_GET_LINEHANDLE_IOCTL"
			<< ret << " " << strerror(errno);
		return ret;
	}

	gpiohandle_data data;
	ret = ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
	if (ret == -1)
		ret = -errno;

	if (ret < 0) {
		LOG(ERROR) << "Failed to issue GPIOHANDLE_GET_LINE_VALUES_IOCTL"
			<< ret << " " << strerror(errno);
		close(req.fd);
		return ret;
	}

	close(req.fd);

	return data.values[0] | (data.values[1] << 1) | (data.values[2] << 2);
}

static void LoadProperties(char* data, const char* filter, const char* filename,
                           std::map<std::string, std::string>* properties) {
    char *key, *value, *eol, *sol, *tmp, *fn;
    size_t flen = 0;

    if (filter) {
        flen = strlen(filter);
    }

    sol = data;
    while ((eol = strchr(sol, '\n'))) {
        key = sol;
        *eol++ = 0;
        sol = eol;

        while (isspace(*key)) key++;
        if (*key == '#') continue;

        tmp = eol - 2;
        while ((tmp > key) && isspace(*tmp)) *tmp-- = 0;

		value = strchr(key, '=');
		if (!value) continue;
		*value++ = 0;

		tmp = value - 2;
		while ((tmp > key) && isspace(*tmp)) *tmp-- = 0;

		while (isspace(*value)) value++;

		if (flen > 0) {
			if (filter[flen - 1] == '*') {
				if (strncmp(key, filter, flen - 1) != 0) continue;
			} else {
				if (strcmp(key, filter) != 0) continue;
			}
		}

		if (StartsWith(key, "ctl.") || std::string{key} == "sys.powerctl" ||
			std::string{key} == kRestoreconProperty) {
			LOG(ERROR) << "Ignoring disallowed property '" << key
						<< "' with special meaning in prop file '" << filename << "'";
			continue;
		}

		ucred cr = {.pid = 1, .uid = 0, .gid = 0};
		std::string error;
		auto it = properties->find(key);
		if (it == properties->end()) {
			(*properties)[key] = value;
		} else if (it->second != value) {
			LOG(WARNING) << "Overriding previous property '" << key << "':'" << it->second
							<< "' with new value '" << value << "'";
			it->second = value;
		}
    }
}

static Result<void> load_properties_from_file(const char* filename, const char* filter,
                                              std::map<std::string, std::string>* properties) {
    auto file_contents = ReadFile(filename);
    if (!file_contents.ok()) {
        return Error() << "Couldn't load property file '" << filename
                       << "': " << file_contents.error();
    }
    file_contents->push_back('\n');

    LoadProperties(file_contents->data(), filter, filename, properties);
    return {};
}

static void set_variant()
{
	std::string sku = get_variant_id() == 0 ? "5g" : "wifi";

	property_override("ro.boot.hardware.sku", sku.c_str());

	std::map<std::string, std::string> properties;
	load_properties_from_file(("/product/etc/hardware.sku." + sku + ".prop").c_str(), nullptr, &properties);

	for (const auto& [name, value] : properties)
		property_override(name.c_str(), value.c_str());
}

void vendor_load_properties()
{
    LOG(INFO) << "Loading vendor specific properties";

	if (IsRecoveryMode()) {
		mkdir("/mnt/vendor/persist", 0755);
		mount("/dev/block/by-name/persist", "/mnt/vendor/persist", "ext4", MS_NOATIME | MS_NOSUID | MS_NODEV, "barrier=1");
	}

	mknod("/dev/gpiochip0", S_IFCHR | S_IRUSR | S_IWUSR, makedev(254, 0));

	set_serial();
	set_board_rev();
	set_variant();
}
