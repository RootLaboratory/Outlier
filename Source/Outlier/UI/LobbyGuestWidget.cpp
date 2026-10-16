#include "UI/LobbyGuestWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"

bool ULobbyGuestWidget::GetFrameCenterInLocalSpace(FVector2D& OutCenter) const
{
	if (!Frame)
	{
		return false;
	}

	const FGeometry& GuestGeometry = GetCachedGeometry();
	const FGeometry& FrameGeometry = Frame->GetCachedGeometry();
	if (GuestGeometry.GetLocalSize().IsNearlyZero() || FrameGeometry.GetLocalSize().IsNearlyZero())
	{
		return false;
	}

	// 이동된 Guest의 좌표를 제외하고 배경 중심을 Guest 로컬 좌표로 되돌린다.
	OutCenter = GuestGeometry.AbsoluteToLocal(
		FrameGeometry.LocalToAbsolute(FrameGeometry.GetLocalSize() * 0.5f));
	return true;
}

void ULobbyGuestWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	RefreshText();
	RefreshResultImage();
	RefreshPresentation();
}

void ULobbyGuestWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshText();
	RefreshResultImage();
	RefreshPresentation();
}

void ULobbyGuestWidget::SetGuestIndex(int32 InGuestIndex)
{
	GuestIndex = InGuestIndex;
	RefreshText();
}

void ULobbyGuestWidget::SetGuestState(ELobbyGuestWidgetState InState, bool bInIsLocalGuest)
{
	GuestState = InState;
	bIsOwningLocalGuest = bInIsLocalGuest;
	OnGuestStateChanged(GuestState, bIsOwningLocalGuest, bConfirmed);
	RefreshPresentation();
}

void ULobbyGuestWidget::SetConfirmed(bool bInConfirmed)
{
	if (bConfirmed == bInConfirmed)
	{
		return;
	}

	bConfirmed = bInConfirmed;
	RefreshResultImage();
	OnGuestStateChanged(GuestState, bIsOwningLocalGuest, bConfirmed);
	RefreshPresentation();
}

void ULobbyGuestWidget::RefreshText()
{
	if (!GuestText)
	{
		return;
	}

	const int32 DisplayIndex = GuestIndex == INDEX_NONE ? 1 : GuestIndex + 1;
	GuestText->SetText(FText::FromString(FString::Printf(TEXT("Player %d"), DisplayIndex)));
}

void ULobbyGuestWidget::RefreshResultImage()
{
	if (!ResultImage)
	{
		return;
	}

	UTexture2D* Texture = bConfirmed ? ConfirmedTexture.Get() : DefaultTexture.Get();
	if (Texture)
	{
		ResultImage->SetBrushFromTexture(Texture);
	}
}

void ULobbyGuestWidget::RefreshPresentation()
{
	const bool bHasSelectedRole = GuestState != ELobbyGuestWidgetState::Default;
	if (GuestText)
	{
		GuestText->SetVisibility(bHasSelectedRole ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (ResultImage)
	{
		ResultImage->SetVisibility(bHasSelectedRole ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (!WidgetTree)
	{
		return;
	}

	if (UImage* LeftArrow = Cast<UImage>(WidgetTree->FindWidget(FName(TEXT("<")))))
	{
		LeftArrow->SetVisibility(GuestState == ELobbyGuestWidgetState::Shooter
			? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (UImage* RightArrow = Cast<UImage>(WidgetTree->FindWidget(FName(TEXT(">")))))
	{
		RightArrow->SetVisibility(GuestState == ELobbyGuestWidgetState::Partner
			? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}
