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
using android::init::IsRecoveryMode;

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
		 * On prototypes the "ad sn" is empty. In this case we fall back to "pcb sn".
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
	property_override("ro.boot.serialno", sn.c_str());
}

static const char *device_props[] = {
	"ro.product.device",
	"ro.product.odm.device",
	"ro.product.product.device",
	"ro.product.system.device",
	"ro.product.system_ext.device",
	"ro.product.vendor.device",
};

static const char *model_props[] = {
	"ro.product.model",
	"ro.product.odm.model",
	"ro.product.product.model",
	"ro.product.system.model",
	"ro.product.system_ext.model",
	"ro.product.vendor.model",
};

static const char *name_props[] = {
	"ro.product.name",
	"ro.product.odm.name",
	"ro.product.product.name",
	"ro.product.system.name",
	"ro.product.system_ext.name",
	"ro.product.vendor.name",
};

static const char *build_fingerprint_props[] = {
	"ro.build.fingerprint",
	"ro.product.build.fingerprint",
	"ro.system.build.fingerprint",
	"ro.system_ext.build.fingerprint",
};

static const char *odm_fingerprint_props[] = {
	"ro.odm.build.fingerprint",
	"ro.vendor.build.fingerprint",
};

static void set_sku_props()
{
	char sku[PROP_VALUE_MAX];
	if (__system_property_get("ro.boot.hardware.sku", sku) <= 0)
		return;

	bool is_5g = std::string(sku) == "5g";

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

	for (int i = 0; i < sizeof(device_props) / sizeof(device_props[0]); i++)
		property_override(device_props[i], device.c_str());

	for (int i = 0; i < sizeof(model_props) / sizeof(model_props[0]); i++)
		property_override(model_props[i], model.c_str());

	for (int i = 0; i < sizeof(name_props) / sizeof(name_props[0]); i++)
		property_override(name_props[i], name.c_str());

	for (int i = 0; i < sizeof(build_fingerprint_props) / sizeof(build_fingerprint_props[0]); i++)
		property_override(build_fingerprint_props[i], build_fingerprint.c_str());

	for (int i = 0; i < sizeof(odm_fingerprint_props) / sizeof(odm_fingerprint_props[0]); i++)
		property_override(odm_fingerprint_props[i], odm_fingerprint.c_str());

}

void vendor_process_bootenv()
{
	if (!IsRecoveryMode())
		return;

	mkdir("/mnt/vendor/persist", 0755);
	mount("/dev/block/by-name/persist", "/mnt/vendor/persist", "ext4", MS_NOATIME | MS_NOSUID | MS_NODEV, "barrier=1");
}

void vendor_load_properties()
{
    LOG(INFO) << "Loading vendor specific properties";

	set_serial();
	set_sku_props();
}
