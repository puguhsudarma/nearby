// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fastpair/keyed_service/fast_pair_mediator.h"

#include <memory>
#include <utility>

#include "fastpair/common/protocol.h"
#include "fastpair/internal/mediums/mediums.h"
#include "fastpair/repository/fast_pair_device_repository.h"
#include "fastpair/scanning/scanner_broker_impl.h"
#include "fastpair/server_access/fast_pair_repository_impl.h"
#include "fastpair/ui/actions.h"
#include "fastpair/ui/fast_pair/fast_pair_notification_controller.h"
#include "fastpair/ui/ui_broker_impl.h"
#include "internal/platform/logging.h"
#include "internal/platform/single_thread_executor.h"

namespace nearby {
namespace fastpair {

Mediator::Mediator(
    std::unique_ptr<Mediums> mediums, std::unique_ptr<UIBroker> ui_broker,
    std::unique_ptr<FastPairNotificationController> notification_controller,
    std::unique_ptr<FastPairRepository> fast_pair_repository,
    std::unique_ptr<SingleThreadExecutor> executor)
    : mediums_(std::move(mediums)),
      ui_broker_(std::move(ui_broker)),
      notification_controller_(std::move(notification_controller)),
      fast_pair_repository_(std::move(fast_pair_repository)),
      executor_(std::move(executor)) {
  devices_ = std::make_unique<FastPairDeviceRepository>(executor_.get());
  scanner_broker_ = std::make_unique<ScannerBrokerImpl>(
      *mediums_, executor_.get(), devices_.get());
  scanner_broker_->AddObserver(this);
  ui_broker_->AddObserver(this);
}

void Mediator::OnDeviceFound(FastPairDevice& device) {
  NEARBY_LOGS(INFO) << __func__ << ": " << device;

  if (IsDeviceCurrentlyShowingNotification(device)) {
    NEARBY_LOGS(VERBOSE) << __func__
                         << ": Extending notification for re-discovered device="
                         << *device_currently_showing_notification_;
    // TODO(b/278768167): Add ui_broker_->ExtendNotification();
    return;
  } else if (device_currently_showing_notification_) {
    NEARBY_LOGS(VERBOSE)
        << __func__
        << ": Already showing a notification for a different device= "
        << *device_currently_showing_notification_;
    return;
  }
  // Show discovery notification
  device_currently_showing_notification_ = &device;
  ui_broker_->ShowDiscovery(device, *notification_controller_);
}

void Mediator::OnDeviceLost(FastPairDevice& device) {
  NEARBY_LOGS(INFO) << __func__ << ": " << device;
}

void Mediator::OnDiscoveryAction(const FastPairDevice& device,
                                 DiscoveryAction action) {
  switch (action) {
    case DiscoveryAction::kPairToDevice:
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kPairToDevice";
      // TODO(285451051): Adding show pairing for higher than v1 version in ui
      // broker
      // TODO(282022590): Adding pairer broker to pair device
      break;
    case DiscoveryAction::kDismissedByOs:
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kDismissedByOs";
      break;
    case DiscoveryAction::kDismissedByUser:
      // When the user explicitly dismisses the discovery notification, update
      // the device's block-list value accordingly.
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kDismissedByUser";
      // TODO(285453663): update discovery block list
      [[fallthrough]];
    case DiscoveryAction::kDismissedByTimeout:
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kDismissedByTimeout";
      device_currently_showing_notification_ = nullptr;
      break;
    case DiscoveryAction::kLearnMore:
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kLearnMore";
      break;
    default:
      NEARBY_LOGS(INFO) << __func__ << ": Action =  kUnknow";
      break;
  }
}

void Mediator::StartScanning() {
  if (IsFastPairEnabled()) {
    scanning_session_ =
        scanner_broker_->StartScanning(Protocol::kFastPairInitialPairing);
    return;
  }
  scanning_session_.reset();
}

bool Mediator::IsFastPairEnabled() {
  // TODO(b/275452353): Add feature_status_tracker IsFastPairEnabled()
  // Currently default to true.
  NEARBY_LOGS(VERBOSE) << __func__ << ": " << true;
  return true;
}

bool Mediator::IsDeviceCurrentlyShowingNotification(
    const FastPairDevice& device) {
  // BLE addresses could have rotated, causing this check to return false for
  // the same device. Fast Pair considers a device different if they have
  // different BLE addresses. Similarly, the this check will fail if it is the
  // same physical device under different scenarios: for example, if a device
  // is found via the initial scenario and via the subsequent scenario, Fast
  // Pair does not consider them the same device.

  return device_currently_showing_notification_ &&
         device_currently_showing_notification_->GetModelId() ==
             device.GetModelId() &&
         device_currently_showing_notification_->GetBleAddress() ==
             device.GetBleAddress() &&
         device_currently_showing_notification_->GetProtocol() !=
             device.GetProtocol();
}

}  // namespace fastpair
}  // namespace nearby
