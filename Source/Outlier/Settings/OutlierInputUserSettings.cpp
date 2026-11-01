#include "Settings/OutlierInputUserSettings.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"

namespace
{
	EOutlierSprintInputMode NormalizeSprintInputMode(EOutlierSprintInputMode Mode)
	{
		return Mode == EOutlierSprintInputMode::Hold
			? EOutlierSprintInputMode::Hold : EOutlierSprintInputMode::Toggle;
	}
}

void UOutlierInputUserSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		// 한 역할의 손상된 값 때문에 다른 역할이나 기존 키 프로필을 초기화하지 않는다.
		ShooterSprintInputMode = NormalizeSprintInputMode(ShooterSprintInputMode);
		PartnerSprintInputMode = NormalizeSprintInputMode(PartnerSprintInputMode);
	}
}

EOutlierSprintInputMode UOutlierInputUserSettings::GetSprintInputMode(EOutlierPlayerRole Role) const
{
	if (Role == EOutlierPlayerRole::Shooter) return NormalizeSprintInputMode(ShooterSprintInputMode);
	if (Role == EOutlierPlayerRole::Partner) return NormalizeSprintInputMode(PartnerSprintInputMode);
	return EOutlierSprintInputMode::Toggle;
}

bool UOutlierInputUserSettings::SetSprintInputMode(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode)
{
	if (Mode != EOutlierSprintInputMode::Toggle && Mode != EOutlierSprintInputMode::Hold) return false;
	if (Role == EOutlierPlayerRole::Shooter) ShooterSprintInputMode = Mode;
	else if (Role == EOutlierPlayerRole::Partner) PartnerSprintInputMode = Mode;
	else return false;
	return true;
}

void UOutlierInputUserSettings::HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettingsObject)
{
	// 기본 구현이 현재 키 프로필과 리바인딩을 이전한다. 새 역할 필드는 생성 기본값 Toggle을 유지한다.
	Super::HandleSettingsClassChangedOnLoad_Impl(OldSettingsObject);
}
