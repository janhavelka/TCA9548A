#include "TCA9548A/TCA9548A.h"

int main() {
  TCA9548A::Config cfg;
  TCA9548A::TCA9548A mux;
  TCA9548A::SettingsSnapshot snapshot;

  (void)cfg;
  (void)mux.getSettings(snapshot);
  return 0;
}
