#pragma once

#include "CoreMinimal.h"
#include "LunarTypes.generated.h"

UENUM(BlueprintType)
enum LunarMovementMode : int
{
	CMOVE_Slide				UMETA(DisplayName="Slide"),
	CMOVE_AirSlide			UMETA(DisplayName="Aerial Slide"),
};
