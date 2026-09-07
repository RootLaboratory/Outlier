#include "UI/LobbyGuestWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void ULobbyGuestWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	RefreshText();
	RefreshResultImage();
}

void ULobbyGuestWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshText();
	RefreshResultImage();
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
}

void ULobbyGuestWidget::RefreshText()
{
	if (!GuestText)
	{
		return;
	}

	const int32 DisplayIndex = GuestIndex == INDEX_NONE ? 0 : GuestIndex + 1;
	GuestText->SetText(FText::FromString(FString::Printf(TEXT("Guest %d"), DisplayIndex)));
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
