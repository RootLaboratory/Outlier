// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "FPostProcessStructures.h"
#include "LocalPlayerPostProcessSubsystem.generated.h"

class FOutlierPostProcessSceneViewExtension;

struct FPPGameplayState
{
	uint8 bIsSliding : 1 = false;
};

UCLASS()
class RDG_API ULocalPlayerPostProcessSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ActivateSlideState();
	void DeActivateSlideState();

	void ActivateChromaticAberration();
	void DeactivateChromaticAberration();
	void SetChromaticAberrationEnabled(bool bEnabled);
	void SetChromaticAberrationStartOffset(float InStartOffset);
	void SetChromaticAberrationIntensity(float InIntensity);

	void TickFrame();
	const FPostProcessStrcture& GetPostProcessStrcture();
	const FPostProcessStrctureUI& GetUIPostProcessStrcture() const;
	bool IsDirty();

private:
	void MarkDirty();

	FPPGameplayState PlayerState;
	FPostProcessStrcture PostProcessParameters;
	FPostProcessStrcture CachedPostProcessParameters;

	FPostProcessStrctureUI CachedUIPostProcessParameters;
	FPostProcessStrctureUI UIPostProcessParameters;

	TSharedPtr<FOutlierPostProcessSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;

	uint8 bDirty : 1 = false;
};
