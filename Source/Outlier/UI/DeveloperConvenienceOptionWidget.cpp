#include "UI/DeveloperConvenienceOptionWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "FirstPerson/FirstPersonPlayerController.h"

#define LOCTEXT_NAMESPACE "DeveloperConvenienceOptionWidget"

void UDeveloperConvenienceOptionWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshLabels();
	if (IsDesignTime() && ONOFF)
	{
		ONOFF->SetText(GetOptionValue(FDeveloperConvenienceOptions()) ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"));
	}
}

void UDeveloperConvenienceOptionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshLabels();
	if (Button)
	{
		Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClicked);
	}
	ObservedController = Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	bContextActive = ObservedController.IsValid();
	if (auto* Controller = ObservedController.Get())
	{
		Controller->OnDeveloperConvenienceStateChanged.AddUniqueDynamic(this, &ThisClass::HandleStateChanged);
		HandleStateChanged(Controller->GetDeveloperConvenienceState());
	}
	else
	{
		HandleStateChanged(FDeveloperConvenienceSnapshot());
	}
}

void UDeveloperConvenienceOptionWidget::NativeDestruct()
{
	// 표시가 끝난 위젯의 클릭이나 상태 알림이 다음 세션을 조작하지 않도록 먼저 비활성화한다.
	bContextActive = false;
	if (auto* Controller = ObservedController.Get())
	{
		Controller->OnDeveloperConvenienceStateChanged.RemoveDynamic(this, &ThisClass::HandleStateChanged);
	}
	ObservedController.Reset();
	if (Button)
	{
		Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleClicked);
	}
	Super::NativeDestruct();
}

void UDeveloperConvenienceOptionWidget::RefreshLabels()
{
	FText DefaultTitle;
	FText DefaultDescription;
	switch (Option)
	{
	case EDeveloperConvenienceOption::Invincible:
		DefaultTitle = LOCTEXT("InvincibleTitle", "무적 모드");
		DefaultDescription = LOCTEXT("InvincibleDescription", "두 플레이어 피해 무효");
		break;
	case EDeveloperConvenienceOption::ConsumeCurrency:
		DefaultTitle = LOCTEXT("CurrencyTitle", "재화 소비");
		DefaultDescription = LOCTEXT("CurrencyDescription", "OFF일 때 구매 비용 면제");
		break;
	case EDeveloperConvenienceOption::FastCooldown:
		DefaultTitle = LOCTEXT("CooldownTitle", "빠른 재사용");
		DefaultDescription = LOCTEXT("CooldownDescription", "두 플레이어 스킬 쿨타임 1초");
		break;
	}
	if (Title)
	{
		Title->SetText(TitleText.IsEmpty() ? DefaultTitle : TitleText);
	}
	if (Desc)
	{
		Desc->SetText(DescriptionText.IsEmpty() ? DefaultDescription : DescriptionText);
	}
}

bool UDeveloperConvenienceOptionWidget::GetOptionValue(const FDeveloperConvenienceOptions& Options) const
{
	switch (Option)
	{
	case EDeveloperConvenienceOption::Invincible:
		return Options.bInvincible;
	case EDeveloperConvenienceOption::ConsumeCurrency:
		return Options.bConsumeCurrency;
	case EDeveloperConvenienceOption::FastCooldown:
		return Options.bFastCooldown;
	default:
		return false;
	}
}

void UDeveloperConvenienceOptionWidget::HandleStateChanged(const FDeveloperConvenienceSnapshot& Snapshot)
{
	Context = Snapshot;
	if (ONOFF)
	{
		ONOFF->SetText(GetOptionValue(Context.Options) ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"));
	}
	if (Button)
	{
		Button->SetIsEnabled(bContextActive && Context.MatchId.IsValid() && Context.SessionId.IsValid()
			&& Context.bIsOperator && !Context.bTransitioning);
	}
}

void UDeveloperConvenienceOptionWidget::HandleClicked()
{
	if (!bContextActive || !Context.MatchId.IsValid() || !Context.SessionId.IsValid()
		|| !Context.bIsOperator || Context.bTransitioning)
	{
		return;
	}
	if (auto* Controller = ObservedController.Get())
	{
		// 클릭 직후 표시를 뒤집지 않는다. 승인된 서버 상태가 양쪽 UI의 표시 기준이다.
		Controller->RequestDeveloperConvenienceOption(Option, !GetOptionValue(Context.Options), Context);
	}
}

#undef LOCTEXT_NAMESPACE
