/*
 * Copyright (C) 2019 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "FingerprintInscreenService"

#include "FingerprintInscreen.h"

#include <android-base/file.h>
#include <android-base/logging.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <cmath>

#define FINGERPRINT_ERROR_VENDOR 8
#define FINGERPRINT_ACQUIRED_VENDOR 6

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 3
#define PARAM_NIT_NONE 0

#define DISPPARAM_PATH "/sys/devices/platform/soc/5e00000.qcom,mdss_mdp/drm/card0/card0-DSI-1/disp_param"
#define DISPPARAM_FOD_BACKLIGHT_HBM "0x1d007ff"
#define DISPPARAM_FOD_BACKLIGHT_RESET "0x20f0000"

#define FOD_STATUS_PATH "/sys/class/touch/tp_dev/fod_status"
#define FOD_STATUS_ON 1
#define FOD_STATUS_OFF 0

#define FOD_SENSOR_X 293
#define FOD_SENSOR_Y 1356
#define FOD_SENSOR_SIZE 134

#define FOD_UI_PATH "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_ui"
#define DIM_LAYER_OFF_DELAY 85ms

#define FOD_SENSOR_X 445
#define FOD_SENSOR_Y 1931
#define FOD_SENSOR_SIZE 190

#define DIM_LAYER_HBM_PATH "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/dimlayer_hbm"

using ::android::base::WriteStringToFile;
using namespace std::chrono_literals;

namespace {

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

// Write value to path and close file.
bool WriteToFile(const std::string& path, uint32_t content) {
    return WriteStringToFile(std::to_string(content), path);
}
}  // anonymous namespace
namespace vendor {
namespace lineage {
namespace biometrics {
namespace fingerprint {
namespace inscreen {
namespace V1_0 {
namespace implementation {

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

FingerprintInscreen::FingerprintInscreen() {
    xiaomiFingerprintService = IXiaomiFingerprint::getService();
}

Return<int32_t> FingerprintInscreen::getPositionX() {
    return FOD_SENSOR_X;
}

Return<int32_t> FingerprintInscreen::getPositionY() {
    return FOD_SENSOR_Y;
}

Return<int32_t> FingerprintInscreen::getSize() {
    return FOD_SENSOR_SIZE;
}

Return<void> FingerprintInscreen::onStartEnroll() {
    return Void();
}

Return<void> FingerprintInscreen::onFinishEnroll() {
    return Void();
}

Return<void> FingerprintInscreen::onPress() {
    set(DISPPARAM_PATH, DISPPARAM_FOD_BACKLIGHT_HBM);
    xiaomiFingerprintService->extCmd(COMMAND_NIT, PARAM_NIT_FOD);
    WriteToFile(DIM_LAYER_HBM_PATH, 1);
    return Void();
}

Return<void> FingerprintInscreen::onRelease() {
    set(DISPPARAM_PATH, DISPPARAM_FOD_BACKLIGHT_RESET);
    xiaomiFingerprintService->extCmd(COMMAND_NIT, PARAM_NIT_NONE);
    return Void();
}

Return<void> FingerprintInscreen::onShowFODView() {
    set(FOD_STATUS_PATH, FOD_STATUS_ON);
    WriteToFile(DIM_LAYER_HBM_PATH, 1);
    return Void();
}

Return<void> FingerprintInscreen::onHideFODView() {
    set(FOD_STATUS_PATH, FOD_STATUS_OFF);
    set(DISPPARAM_PATH, DISPPARAM_FOD_BACKLIGHT_RESET);
    xiaomiFingerprintService->extCmd(COMMAND_NIT, PARAM_NIT_NONE);
    std::this_thread::sleep_for(DIM_LAYER_OFF_DELAY);
    WriteToFile(DIM_LAYER_HBM_PATH, 0);
    return Void();
}

Return<bool> FingerprintInscreen::handleAcquired(int32_t acquiredInfo, int32_t vendorCode) {
    bool handled = false;

    std::lock_guard<std::mutex> _lock(mCallbackLock);
    if (mCallback == nullptr) {
        return false;
    }

    switch (acquiredInfo) {
        case FINGERPRINT_ACQUIRED_VENDOR:
            if (vendorCode == 22) {
                Return<void> ret = mCallback->onFingerDown();
                if (!ret.isOk()) {
                    LOG(ERROR) << "FingerDown() error: " << ret.description();
                }
                handled = true;
            }

            if (vendorCode == 23) {
                Return<void> ret = mCallback->onFingerUp();
                if (!ret.isOk()) {
                    LOG(ERROR) << "FingerUp() error: " << ret.description();
                }
                handled = true;
            }
            break;
    }

    if (!handled)
        LOG(ERROR) << "acquiredInfo: " << acquiredInfo << ", vendorCode: " << vendorCode;

    return handled;
}

Return<bool> FingerprintInscreen::handleError(int32_t error, int32_t vendorCode) {
    LOG(ERROR) << "error: " << error << ", vendorCode: " << vendorCode << "\n";
    return error == FINGERPRINT_ERROR_VENDOR && vendorCode == 6;
}

Return<void> FingerprintInscreen::setLongPressEnabled(bool) {
    return Void();
}

Return<int32_t> FingerprintInscreen::getDimAmount(int32_t brightness) {
    float alpha;

    if (brightness > 62.0) {
        alpha = 1.0 - pow((((brightness / 255.0) * 430.0) / 600.0), 0.455);
    } else {
        alpha = 1.0 - pow((brightness / 210.0), 0.455);
    }

    return 255 * alpha;
}

Return<bool> FingerprintInscreen::shouldBoostBrightness() {
    return false;
}

Return<void> FingerprintInscreen::setCallback(const sp<::vendor::lineage::biometrics::fingerprint::inscreen::V1_0::IFingerprintInscreenCallback>& callback) {
    {
        std::lock_guard<std::mutex> _lock(mCallbackLock);
        mCallback = callback;
    }

    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace inscreen
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace lineage
}  // namespace vendor
