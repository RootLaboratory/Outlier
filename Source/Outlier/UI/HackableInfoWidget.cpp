#include "UI/HackableInfoWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackInfoRow.h"
#include "Drone/Partner/HackInfoSubsystem.h"
#include "Engine/GameInstance.h"
#include "PopupRetainerBox.h"
#include "GameFramework/Actor.h"

void UHackableInfoWidget::InitializeInfo(AActor* InTargetActor, UHackableComponent* InHackableComponent)
{
	TargetActor = InTargetActor;
	HackableComponent = InHackableComponent;

	UpdateInfoText();
	SetHackProgress(0.0f);
}

void UHackableInfoWidget::SetHackProgress(float InProgress)
{
	if (HackingProgressbar)
	{
		HackingProgressbar->SetPercent(FMath::Clamp(InProgress, 0.0f, 1.0f));
	}
}

void UHackableInfoWidget::PlayPopup()
{
	if (PopupRetainerBox)
	{
		PopupRetainerBox->PlayOpen();
	}
}

void UHackableInfoWidget::ResetPopup()
{
	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
	}
}

FVector2D UHackableInfoWidget::GetOffsetForDistance(float Distance) const
{
	const float DistanceRange = FMath::Max(FarDistance - NearDistance, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((Distance - NearDistance) / DistanceRange, 0.0f, 1.0f);
	return FMath::Lerp(MinOffset, MaxOffset, Alpha);
}

void UHackableInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	ResetPopup();
	UpdateInfoText();
	SetHackProgress(0.0f);
}

bool UHackableInfoWidget::ResolveHackInfoTag(FGameplayTag& OutHackInfoTag) const
{
	OutHackInfoTag = FGameplayTag();
	if (!HackableComponent)
	{
		return false;
	}

	const FGameplayTag HackInfoRootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Hack.Info")), false);
	if (!HackInfoRootTag.IsValid())
	{
		return false;
	}

	TArray<FGameplayTag> HackTagArray;
	HackableComponent->HackTags.GetGameplayTagArray(HackTagArray);
	for (const FGameplayTag& HackTag : HackTagArray)
	{
		if (HackTag.MatchesTag(HackInfoRootTag) && HackTag != HackInfoRootTag)
		{
			OutHackInfoTag = HackTag;
			return true;
		}
	}

	return false;
}

void UHackableInfoWidget::UpdateInfoText()
{
	if (!TitleText)
	{
		return;
	}

	FHackInfoRow HackInfo;
	FGameplayTag HackInfoTag;
	const UGameInstance* GameInstance = GetGameInstance();
	const UHackInfoSubsystem* HackInfoSubsystem = GameInstance ? GameInstance->GetSubsystem<UHackInfoSubsystem>() : nullptr;
	if (ResolveHackInfoTag(HackInfoTag)
		&& HackInfoSubsystem
		&& HackInfoSubsystem->TryGetHackInfo(HackInfoTag, HackInfo))
	{
		CurrentHackInfoTag = HackInfoTag;
		TitleText->SetText(HackInfo.DisplayText);
		return;
	}

	CurrentHackInfoTag = FGameplayTag();
	TitleText->SetText(TargetActor ? FText::FromString(TargetActor->GetName()) : FText::GetEmpty());
}
