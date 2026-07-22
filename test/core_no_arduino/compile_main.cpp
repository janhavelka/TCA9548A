#include <cstdint>
#include <type_traits>

#include "TCA9548A/CommandTable.h"
#include "TCA9548A/Config.h"
#include "TCA9548A/Status.h"
#include "TCA9548A/TCA9548A.h"

static_assert(sizeof(TCA9548A::ChannelMask) == sizeof(uint8_t));
static_assert(sizeof(TCA9548A::Status) <= 24U);
static_assert(sizeof(TCA9548A::TransportStatus) <= 8U);
static_assert(sizeof(TCA9548A::ChannelMaskObservation) <= 2U);
static_assert(sizeof(TCA9548A::TCA9548A) <= 192U);

static_assert(std::is_trivially_copyable<TCA9548A::ChannelMask>::value);
static_assert(std::is_trivially_copyable<TCA9548A::TransportStatus>::value);
static_assert(
    std::is_trivially_copyable<TCA9548A::ChannelMaskObservation>::value);
static_assert(std::is_trivially_copyable<TCA9548A::Status>::value);

static_assert(!std::is_copy_constructible<TCA9548A::TCA9548A>::value);
static_assert(!std::is_copy_assignable<TCA9548A::TCA9548A>::value);
static_assert(!std::is_move_constructible<TCA9548A::TCA9548A>::value);
static_assert(!std::is_move_assignable<TCA9548A::TCA9548A>::value);

static_assert(TCA9548A::cmd::isValidAddress(0x70));
static_assert(TCA9548A::cmd::isValidAddress(0x77));
static_assert(!TCA9548A::cmd::isValidAddress(0x6F));
static_assert(TCA9548A::cmd::addressFromPins(true, true, true) == 0x77);
static_assert(TCA9548A::ChannelMask::one(TCA9548A::Channel::CH7).raw() ==
              0x80);

int main() {
  TCA9548A::TCA9548A mux;
  const TCA9548A::ChannelMaskObservation observation =
      mux.channelMaskObservation();
  return observation.known() ? 1 : 0;
}
