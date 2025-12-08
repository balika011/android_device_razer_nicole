/*
   Copyright (C) 2007, The Android Open Source Project
   Copyright (c) 2016, The CyanogenMod Project
   Copyright (c) 2017, The LineageOS Project

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
   OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
   IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

using android::base::Trim;
using android::base::ReadFileToString;
using android::init::IsRecoveryMode;

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
	int fd = open("/dev/gpiochip0", O_WRONLY, 0777);

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
	int fd = open("/dev/gpiochip0", O_WRONLY, 0777);

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

static void set_variant()
{
	bool is_5g = get_variant_id() == 0;

	std::string device = "Razer-Edge-WiFi";
	std::string model = "Razer Edge WiFi";
	std::string name = "Nicole";
	std::string build_fingerprint = "Razer/Nicole/Razer-Edge-WiFi:12/SKQ1.211103.001/172:user/release-keys";
	std::string odm_fingerprint = "Razer/Nicole/Razer-Edge-WiFi:11/RKQ1.211130.001/172:user/release-keys";

	if (is_5g) {
		device = "RZ45-0460";
		model = "Razer Edge 5G";
		name = "VZW-RZ45-0460";
		build_fingerprint = "Razer/VZW-RZ45-0460/RZ45-0460:12/SKQ1.211103.001/155:user/release-keys";
		odm_fingerprint = "Razer/VZW-RZ45-0460/RZ45-0460:11/RKQ1.211130.001/155:user/release-keys";
	}

	const char *device_props[] = {
		"ro.product.device",
		"ro.product.odm.device",
		"ro.product.product.device",
		"ro.product.system.device",
		"ro.product.system_ext.device",
		"ro.product.vendor.device",
	};

	for (int i = 0; i < sizeof(device_props) / sizeof(device_props[0]); i++)
		property_override(device_props[i], device.c_str());

	const char *model_props[] = {
		"ro.product.model",
		"ro.product.odm.model",
		"ro.product.product.model",
		"ro.product.system.model",
		"ro.product.system_ext.model",
		"ro.product.vendor.model",
	};

	for (int i = 0; i < sizeof(model_props) / sizeof(model_props[0]); i++)
		property_override(model_props[i], model.c_str());

	const char *name_props[] = {
		"ro.product.name",
		"ro.product.odm.name",
		"ro.product.product.name",
		"ro.product.system.name",
		"ro.product.system_ext.name",
		"ro.product.vendor.name",
	};

	for (int i = 0; i < sizeof(name_props) / sizeof(name_props[0]); i++)
		property_override(name_props[i], name.c_str());

	const char *build_fingerprint_props[] = {
		"ro.build.fingerprint",
		"ro.product.build.fingerprint",
		"ro.system.build.fingerprint",
		"ro.system_ext.build.fingerprint",
	};

	for (int i = 0; i < sizeof(build_fingerprint_props) / sizeof(build_fingerprint_props[0]); i++)
		property_override(build_fingerprint_props[i], build_fingerprint.c_str());

	const char *odm_fingerprint_props[] = {
		"ro.odm.build.fingerprint",
		"ro.vendor.build.fingerprint",
	};

	for (int i = 0; i < sizeof(odm_fingerprint_props) / sizeof(odm_fingerprint_props[0]); i++)
		property_override(odm_fingerprint_props[i], odm_fingerprint.c_str());

	if (is_5g) {
		property_override("vendor.rild.libpath", "/vendor/lib64/hw/libquectel-ril.so");
		property_override("rild.libpath", "/vendor/lib64/hw/libquectel-ril.so");
		property_override("persist.vendor.radio.uicc_se_enabled", "1");
		property_override("persist.radio.multisim.config", "ssss");

		property_override("persist.vendor.radio.apm_sim_not_pwdn", "1");
		property_override("persist.vendor.radio.sib16_support", "1");
		property_override("persist.vendor.radio.custom_ecc", "1");
		property_override("persist.vendor.radio.procedure_bytes", "SKIP");
		property_override("persist.vendor.radio.rat_on", "combine");
	} else
		property_override("ro.radio.noril", "1");
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
