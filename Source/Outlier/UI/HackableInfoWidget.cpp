#include "UI/HackableInfoWidget.h"

#include "Audio/OutlierAudioFeedback.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackInfoRow.h"
#include "Drone/Partner/HackInfoSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "PopupRetainerBox.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"

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
	SetPopupAudioOpen(true);
}

void UHackableInfoWidget::ResetPopup()
{
	bHasHudTracking = false;
	if (PopupRetainerBox)
	{
		PopupRetainerBox->ResetPopup();
	}
	SetPopupAudioOpen(false);
}

void UHackableInfoWidget::InitializeHudTracking(const FTargetPointHudSettings& Settings, FVector2D MarkerSize,
	FVector2D MarkerPosition, FVector2D Position, FVector2D Size, float Scale)
{
	UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!InfoSlot)
	{
		return;
	}
	// 홀드로 넘어가면 후보 마커가 제거되어도 같은 배치 설정과 오프셋으로 추적한다.
	HudTrackingSettings = Settings;
	HudTrackingMarkerSize = MarkerSize;
	HudScreenOffset = Position - MarkerPosition;
	bHasHudTracking = true;
	InfoSlot->SetAutoSize(false);
	InfoSlot->SetAlignment(FVector2D::ZeroVector);
	InfoSlot->SetPosition(Position);
	InfoSlot->SetSize(Size);
	InfoSlot->SetZOrder(10);
	SetRenderTransformPivot(FVector2D::ZeroVector);
	SetRenderScale(FVector2D(Scale));
}

void UHackableInfoWidget::UpdateHudTracking()
{
	if (!bHasHudTracking)
	{
		return;
	}
	FVector2D MarkerPosition;
	float MarkerScale;
	if (!IsValid(TargetActor)
		|| !HudTrackingSettings.Project(GetOwningPlayer(), IHackableInterface::ResolveHackTargetLocation(TargetActor),
			HudTrackingMarkerSize, MarkerPosition, MarkerScale))
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* InfoSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		// 팝업의 첫 배치만 화면에 맞춘다. 이후에는 공통 앵커와의 간격을 유지한다.
		InfoSlot->SetPosition(MarkerPosition + HudScreenOffset);
	}
}

void UHackableInfoWidget::SetPopupAudioOpen(bool bOpen)
{
	if (bPopupAudioOpen == bOpen)
	{
		return;
	}
	bPopupAudioOpen = bOpen;
	OutlierAudioFeedback::Local(this, bOpen ? TEXT("Audio.Context.UI.Ingame.Open") : TEXT("Audio.Context.UI.Ingame.Close"));
}

void UHackableInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	ResetPopup();
	UpdateInfoText();
	SetHackProgress(0.0f);
}

void UHackableInfoWidget::NativeDestruct()
{
	// 마커가 사라지며 열린 정보창이 함께 닫히는 경우에도 닫힘음을 낸다. 월드 정리 중에는 생략한다.
	const UWorld* World = GetWorld();
	if (World && !World->bIsTearingDown)
	{
		SetPopupAudioOpen(false);
	}
	Super::NativeDestruct();
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
	FHackInfoRow HackInfo;
	FGameplayTag HackInfoTag;
	const UGameInstance* GameInstance = GetGameInstance();
	const UHackInfoSubsystem* HackInfoSubsystem = GameInstance ? GameInstance->GetSubsystem<UHackInfoSubsystem>() : nullptr;
	if (ResolveHackInfoTag(HackInfoTag)
		&& HackInfoSubsystem
		&& HackInfoSubsystem->TryGetHackInfo(HackInfoTag, HackInfo))
	{
		CurrentHackInfoTag = HackInfoTag;
		if (TitleText)
		{
			TitleText->SetText(HackInfo.DisplayText);
		}
		UpdateInfoImage(&HackInfo);
		return;
	}

	CurrentHackInfoTag = FGameplayTag();
	if (TitleText)
	{
		TitleText->SetText(TargetActor ? FText::FromString(TargetActor->GetName()) : FText::GetEmpty());
	}
	UpdateInfoImage(nullptr);
}

void UHackableInfoWidget::UpdateInfoImage(const FHackInfoRow* HackInfo)
{
	if (!hackingInfoImage)
	{
		return;
	}

	if (!bCachedDefaultInfoImageBrush)
	{
		DefaultInfoImageBrush = hackingInfoImage->GetBrush();
		bCachedDefaultInfoImageBrush = true;
	}

	UTexture2D* InfoTexture = HackInfo ? HackInfo->InfoBorderImage.LoadSynchronous() : nullptr;
	if (InfoTexture)
	{
		hackingInfoImage->SetBrushFromTexture(InfoTexture);
		return;
	}

	// info에 이미지가 없으면 WBP에 지정된 기본 브러시로 복구
	hackingInfoImage->SetBrush(DefaultInfoImageBrush);
}
