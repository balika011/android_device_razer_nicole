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
#include <algorithm>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "vendor_init.h"
#include "property_service.h"

using android::base::Trim;
using android::base::ReadFileToString;

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
		if (sn.size() > 32 && strlen(sn.substr(32)) > 0)
			sn = sn.substr(32);
		sn.resize(std::min(32, strlen(sn.c_str())));
		if (sn.empty())
			sn = "0000000";
	} else {
        LOG(ERROR) << "Unable to read serial number";
		sn = "0000000";
	}

	property_override("ro.serial", sn.c_str());
}

static void check_variant()
{
	bool is_5g = false;
	std::string pcba_info;

	ReadFileToString("/sys/devices/virtual/hardware_info/interface/dev_info/pcba_info", &pcba_info) {
		if (pcba_info == "0")
			is_5g = true;
	}

	std::string device = "Razer-Edge-WiFi";
	std::string model = "Razer Edge WiFi";
	std::string name = "Nicole";

	if (is_5g) {
		device = "RZ45-0460";
		model = "Razer Edge 5G";
		name = "VZW-RZ45-0460";
	}

	property_override("ro.product.device", device.c_str());
	property_override("ro.product.system.device", device.c_str());
	property_override("ro.product.vendor.device", device.c_str());

	property_override("ro.product.model", model.c_str());
	property_override("ro.product.system.model", model.c_str());
	property_override("ro.product.vendor.model", model.c_str());

	property_override("ro.product.name", name.c_str());
	property_override("ro.product.system.name", name.c_str());
	property_override("ro.product.vendor.name", name.c_str());

	if (!is_5g) {
		property_override("ro.system.build.fingerprint", "Razer/Nicole/Razer-Edge-WiFi:12/SKQ1.211103.001/172:user/release-keys");
		property_override("ro.vendor.build.fingerprint", "Razer/Nicole/Razer-Edge-WiFi:11/RKQ1.211130.001/172:user/release-keys");

		property_override("ro.modemtype.wifi", "true");
		property_override("ro.radio.noril", "true");

		property_override("persist.vendor.radio.uicc_se_enabled", "0");
	} else {
		property_override("ro.system.build.fingerprint", "Razer/VZW-RZ45-0460/RZ45-0460:12/SKQ1.211103.001/155:user/release-keys");
		property_override("ro.vendor.build.fingerprint", "Razer/VZW-RZ45-0460/RZ45-0460:11/RKQ1.211130.001/155:user/release-keys");

		property_override("ro.modemtype.wifi", "false");
		property_override("ro.radio.noril", "false");
		property_override("ro.vendor.radio.noril", "no");

		property_override("vendor.rild.libpath", "/vendor/lib64/hw/libquectel-ril.so");
		property_override("rild.libpath", "/vendor/lib64/hw/libquectel-ril.so");
		property_override("persist.vendor.radio.uicc_se_enabled", "1");
		property_override("persist.radio.multisim.config", "ssss");

		property_override("persist.vendor.radio.apm_sim_not_pwdn", "1");
		property_override("persist.vendor.radio.sib16_support", "1");
		property_override("persist.vendor.radio.custom_ecc", "1");
		property_override("persist.vendor.radio.procedure_bytes", "SKIP");
		property_override("persist.vendor.radio.rat_on", "combine");
	}
}

void vendor_load_properties()
{
    LOG(INFO) << "Loading vendor specific properties";
	set_serial();
	check_variant();
}
