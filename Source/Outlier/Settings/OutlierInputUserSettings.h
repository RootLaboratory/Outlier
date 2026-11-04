#pragma once

#include "CoreMinimal.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "OutlierInputUserSettings.generated.h"

enum class EOutlierPlayerRole : uint8;
class UOutlierInputBindingTable;
struct FOutlierInputBindingTableRow;

enum class EOutlierLinkedBindingResult : uint8
{
	Success,
	InvalidData,
	InputRequired,
	AxisInputRequired,
	Conflict,
	Unavailable,
	Failed
};

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

	// 프로필만 변경한다. 성공 이후의 Apply/Save와 UI 통지는 소유 Subsystem이 한 번 수행한다.
	EOutlierLinkedBindingResult ChangeLinkedBinding(const UOutlierInputBindingTable* Table,
		const FOutlierInputBindingTableRow& Row, FKey NewKey, FString& OutError);
	bool GetBindingKey(const FOutlierInputBindingTableRow& Row, FKey& OutKey) const;
	bool ChooseLinkedBindingKey(const UOutlierInputBindingTable* Table,
		const FOutlierInputBindingTableRow& Row, FKey& OutKey, bool& bOutNeedsSync, FString& OutError) const;
	bool RegisterBindingContexts(const UOutlierInputBindingTable* Table);
	EOutlierLinkedBindingResult ValidateLinkedBinding(const UOutlierInputBindingTable* Table,
		const FOutlierInputBindingTableRow& Row, FKey NewKey, FString& OutError) const;

protected:
	virtual void HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettingsObject) override;

private:
	friend struct FOutlierSprintInputSettingsTestAccess;
	bool bChangingLinkedBinding = false;

	UPROPERTY(SaveGame)
	EOutlierSprintInputMode ShooterSprintInputMode = EOutlierSprintInputMode::Toggle;

	UPROPERTY(SaveGame)
	EOutlierSprintInputMode PartnerSprintInputMode = EOutlierSprintInputMode::Toggle;
};
