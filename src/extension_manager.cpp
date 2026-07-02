#include "extension_manager.h"

#include "weather_service.h"

void extensionInit()
{
  weatherInit();
}

void extensionBeforePetUpdate(PetInput &input, uint32_t nowMs)
{
  if (input.displayTogglePressed)
  {
    weatherToggleDisplayPage();
  }

  weatherPoll(nowMs);
}

void extensionAfterPetUpdate(const PetState &state, const PetEventQueue &events, uint32_t nowMs)
{
  (void)state;
  (void)events;
  (void)nowMs;
}
