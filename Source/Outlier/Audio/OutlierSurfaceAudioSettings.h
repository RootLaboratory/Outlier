#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "OutlierSurfaceAudioSettings.generated.h"

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierSurfaceContextMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Surface", meta = (Categories = "Audio.Context"))
	FGameplayTag DefaultContext;
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (Categories = "Audio.Context"))
	FGameplayTag NoneMetalContext;
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (Categories = "Audio.Context"))
	FGameplayTag MetalContext;
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (Categories = "Audio.Context"))
	FGameplayTag GlassContext;

	void GetCandidates(EPhysicalSurface Surface, TArray<FGameplayTag>& OutContexts) const;
};

/** 기존 Context의 표면별 연결만 설정한다. 음원은 기존 Audio Bank에서 관리한다. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Surface Audio"))
class OUTLIER_API UOutlierSurfaceAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category = "Surface", meta = (Categories = "Audio.Context"))
	TMap<FGameplayTag, FOutlierSurfaceContextMapping> ContextMappings;

	/** 태그 등록 이후 호출하며, 명시한 설정은 기존 태그 호환 기본값을 덮어쓴다. */
	void BuildMappings(TMap<FGameplayTag, FOutlierSurfaceContextMapping>& OutMappings) const;
};
