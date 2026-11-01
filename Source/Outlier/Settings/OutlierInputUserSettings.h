#pragma once

#include "CoreMinimal.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "OutlierInputUserSettings.generated.h"

enum class EOutlierPlayerRole : uint8;

UENUM(BlueprintType)
enum class EOutlierSprintInputMode : uint8
{
	Toggle,
	Hold
};

// 키 프로필과 같은 로컬 사용자 파일에 역할별 선호도만 저장한다.
UCLASS()
class OUTLIER_API UOutlierInputUserSettings : public UEnhancedInputUserSettings
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;

	EOutlierSprintInputMode GetSprintInputMode(EOutlierPlayerRole Role) const;
	bool SetSprintInputMode(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode);

protected:
	virtual void HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettingsObject) override;

private:
	friend struct FOutlierSprintInputSettingsTestAccess;

	UPROPERTY(SaveGame)
	EOutlierSprintInputMode ShooterSprintInputMode = EOutlierSprintInputMode::Toggle;

	UPROPERTY(SaveGame)
	EOutlierSprintInputMode PartnerSprintInputMode = EOutlierSprintInputMode::Toggle;
};
