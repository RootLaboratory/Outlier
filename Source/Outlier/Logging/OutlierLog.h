#pragma once

#include "CoreMinimal.h"

namespace OutlierLog
{
enum class EChannel : uint8
{
	Core, Network, Save, Shooter, Animation, Weapon, Enemy, Partner,
	Flight, Ability, Room, Interaction, UI, Audio, World
};

// Process-local diagnostics only. Does not change gameplay or replicate settings.
OUTLIER_API bool IsEnabled(EChannel Channel);
}

// Keep formatting and argument evaluation inside the gate, including in hot paths.
// Real warnings/errors use UE_LOG directly and remain visible with diagnostics off.
#define OUTLIER_LOG_ACTIVE(Channel, Category, Verbosity) \
	(::OutlierLog::IsEnabled(::OutlierLog::EChannel::Channel) && UE_LOG_ACTIVE(Category, Verbosity))

// Put multi-statement, log-only preparation inside if (OUTLIER_LOG_ACTIVE(...)).
#define OUTLIER_LOG(Channel, Category, Verbosity, ...) \
	do \
	{ \
		if (OUTLIER_LOG_ACTIVE(Channel, Category, Verbosity)) \
		{ \
			UE_LOG(Category, Verbosity, __VA_ARGS__); \
		} \
	} while (false)
