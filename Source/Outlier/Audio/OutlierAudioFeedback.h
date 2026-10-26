#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
class AActor;
namespace OutlierAudioFeedback
{
	OUTLIER_API bool ShouldPlayShooterLowHP(bool bLocallyControlled, bool bDead, float Health);
	OUTLIER_API FGameplayTag Tag(const TCHAR* Name);
	OUTLIER_API void Local(const UObject* Context, const TCHAR* AudioContext, const TCHAR* Type = TEXT("Audio.Type.UI"));
	OUTLIER_API void Owner(AActor* Emitter, const TCHAR* AudioContext, const TCHAR* Type = TEXT("Audio.Type.Player"));
	OUTLIER_API void World(AActor* Emitter, const TCHAR* AudioContext, const TCHAR* Type = TEXT("Audio.Type.Interactable"), const FVector* Location = nullptr);
	OUTLIER_API void Loop(AActor* Emitter, const TCHAR* AudioContext, bool bEnabled, const TCHAR* Type = TEXT("Audio.Type.Interactable"), bool bTwoD = false);
	OUTLIER_API void StopLoops(AActor* Emitter);
}
