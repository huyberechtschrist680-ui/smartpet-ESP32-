#include "display_model.h"

#include <stdio.h>

#include "weather_service.h"

DisplayModel makeDisplayModel(const PetState &pet)
{
  DisplayModel model;

  if (pet.power == PowerState::Sleeping)
  {
    model.blank = true;
    return model;
  }

  if (weatherDisplayPageActive() && weatherFillDisplayModel(model))
  {
    return model;
  }

  model.blank = false;
  snprintf(model.line1, kDisplayLineLength, "EMOTION: %d", pet.emotion);

  if (pet.food == FoodState::Full)
  {
    const uint32_t remainingMs =
    pet.fullEndActiveMs > pet.activeMs ? pet.fullEndActiveMs - pet.activeMs : 0;
    const uint32_t totalSeconds = (remainingMs + 999) / 1000;
    const uint32_t minutes = totalSeconds / 60;
    const uint32_t seconds = totalSeconds % 60;
    snprintf(model.line2,
    kDisplayLineLength,
    "FULL %lu Min %02lu s",
    static_cast<unsigned long>(minutes),
    static_cast<unsigned long>(seconds));
  }
  else
  {
    snprintf(model.line2, kDisplayLineLength, "HUNGRY");
  }

  snprintf(model.line3, kDisplayLineLength, "MOTION: %s", motionName(pet.motion));
  return model;
}
