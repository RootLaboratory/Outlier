#include "UI/CinematicHintkey.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"

void UCinematicHintkey::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	// 브러시 머티리얼을 인스턴스별 MID로 교체해 진행도를 개별로 갱신한다.
	SkipButtonMaterial = SkipButtonImage ? SkipButtonImage->GetDynamicMaterial() : nullptr;
	if (SkipButtonImage && !SkipButtonMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] SkipButtonImage brush has no material; hold progress will not be shown."));
	}
	RefreshHint();
}

void UCinematicHintkey::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshHint();
}

void UCinematicHintkey::SetHintText(const FText& InText)
{
	HintMessage = InText;
	RefreshHint();
}

void UCinematicHintkey::SetHoldProgress(float InProgress)
{
	const float Clamped = FMath::Clamp(InProgress, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Clamped, HoldProgress)) return;
	HoldProgress = Clamped;
	ApplyProgressToMaterial();
	OnHoldProgressChanged(HoldProgress);
}

void UCinematicHintkey::RefreshHint()
{
	if (HintText) HintText->SetText(HintMessage);
	ApplyProgressToMaterial();
}

void UCinematicHintkey::ApplyProgressToMaterial()
{
	// 디자이너 미리보기에서는 에셋 머티리얼을 건드리지 않도록 런타임 MID에만 적용한다.
	if (SkipButtonMaterial && !ProgressParameterName.IsNone())
	{
		SkipButtonMaterial->SetScalarParameterValue(ProgressParameterName, HoldProgress);
	}
}
