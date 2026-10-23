#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OutlierMenuAudioBinding.generated.h"
class UButton;
class UOutlierAudioSubsystem;
UCLASS()
class UOutlierMenuAudioBinding : public UObject
{
	GENERATED_BODY()
public:
	void Bind(UButton* InButton, UOutlierAudioSubsystem* InAudio);
	bool Matches(UButton* InButton) const { return Button.Get() == InButton; }
	bool IsAlive() const { return Button.IsValid(); }
private:
	bool bSuppressClick = false;
	TWeakObjectPtr<UButton> Button;
	TWeakObjectPtr<UOutlierAudioSubsystem> Audio;
	UFUNCTION() void Clicked();
	UFUNCTION() void Hovered();
};
