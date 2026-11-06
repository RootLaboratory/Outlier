#include "UI/LobbyGuestWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UI/InputActionKeyDisplayWidget.h"

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
	if (LeftButton)
	{
		LeftButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLeftButtonClicked);
	}
	if (RightButton)
	{
		RightButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRightButtonClicked);
	}

	LeftButtonMaterial = InitializeButtonMaterial(LeftButton);
	RightButtonMaterial = InitializeButtonMaterial(RightButton);
	UpdateButtonAnimations(0.0f);

	RefreshText();
	RefreshResultImage();
	RefreshPresentation();
}

void ULobbyGuestWidget::NativeDestruct()
{
	if (LeftButton)
	{
		LeftButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeftButtonClicked);
	}
	if (RightButton)
	{
		RightButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRightButtonClicked);
	}
	Super::NativeDestruct();
}

UMaterialInstanceDynamic* ULobbyGuestWidget::InitializeButtonMaterial(UButton* Button)
{
	if (!Button)
	{
		return nullptr;
	}

	// 디자이너에서 Normal 브러시에 지정한 머티리얼(M_LobbyButton)을 그대로 원본으로 쓴다.
	FButtonStyle Style = Button->GetStyle();
	UObject* NormalResource = Style.Normal.GetResourceObject();

	// Construct가 다시 불려도 이미 만든 MID를 중복 생성하지 않는다.
	if (UMaterialInstanceDynamic* ExistingMaterial = Cast<UMaterialInstanceDynamic>(NormalResource);
		ExistingMaterial && ExistingMaterial->GetOuter() == this)
	{
		return ExistingMaterial;
	}

	UMaterialInterface* SourceMaterial = Cast<UMaterialInterface>(NormalResource);
	if (!SourceMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* ButtonMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
	if (!ButtonMaterial)
	{
		return nullptr;
	}

	// Hovered/Pressed가 같은 머티리얼을 쓰면 같은 MID를 공유해서 정지 상태가 그대로 보이게 한다.
	// 별도 이미지를 지정한 브러시는 건드리지 않는다.
	Style.Normal.SetResourceObject(ButtonMaterial);
	if (Style.Hovered.GetResourceObject() == SourceMaterial)
	{
		Style.Hovered.SetResourceObject(ButtonMaterial);
	}
	if (Style.Pressed.GetResourceObject() == SourceMaterial)
	{
		Style.Pressed.SetResourceObject(ButtonMaterial);
	}
	if (Style.Disabled.GetResourceObject() == SourceMaterial)
	{
		Style.Disabled.SetResourceObject(ButtonMaterial);
	}
	Button->SetStyle(Style);

	return ButtonMaterial;
}

void ULobbyGuestWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UpdateButtonAnimations(InDeltaTime);
}

void ULobbyGuestWidget::UpdateButtonAnimations(float DeltaTime)
{
	if (ButtonAnimationTimeParameterName.IsNone())
	{
		return;
	}

	const bool bAnyButtonHovered = (LeftButton && LeftButton->IsHovered())
		|| (RightButton && RightButton->IsHovered());
	const bool bAnyButtonActive = (LeftButton && LeftButton->GetIsEnabled() && LeftButton->IsVisible())
		|| (RightButton && RightButton->GetIsEnabled() && RightButton->IsVisible());

	// A shared clock keeps both arrows in sync, including when one arrow is hidden.
	if (bIsOwningLocalGuest && bAnyButtonActive && !bAnyButtonHovered)
	{
		ButtonAnimationTime += FMath::Max(DeltaTime, 0.0f);
	}
	if (LeftButtonMaterial)
	{
		LeftButtonMaterial->SetScalarParameterValue(ButtonAnimationTimeParameterName, ButtonAnimationTime);
	}
	if (RightButtonMaterial)
	{
		RightButtonMaterial->SetScalarParameterValue(ButtonAnimationTimeParameterName, ButtonAnimationTime);
	}
}

void ULobbyGuestWidget::SetArrowButtonVisible(UButton* Button, bool bVisible) const
{
	if (!Button)
	{
		return;
	}

	// Preserve layout so role offsets measured from the frame center remain stable.
	Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
}

void ULobbyGuestWidget::HandleLeftButtonClicked()
{
	if (bIsOwningLocalGuest) OnMoveRequested.Broadcast(-1);
}

void ULobbyGuestWidget::HandleRightButtonClicked()
{
	if (bIsOwningLocalGuest) OnMoveRequested.Broadcast(1);
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

	if (ConfirmedTexture)
	{
		ResultImage->SetBrushFromTexture(ConfirmedTexture.Get());
	}
}

void ULobbyGuestWidget::RefreshPresentation()
{
	if (LeftButton) LeftButton->SetIsEnabled(bIsOwningLocalGuest);
	if (RightButton) RightButton->SetIsEnabled(bIsOwningLocalGuest);

	const bool bHasSelectedRole = GuestState != ELobbyGuestWidgetState::Default;
	if (GuestText)
	{
		GuestText->SetVisibility(bHasSelectedRole ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (ResultImage)
	{
		ResultImage->SetVisibility(bHasSelectedRole && bConfirmed
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (DefaultKey)
	{
		DefaultKey->SetVisibility(bHasSelectedRole && !bConfirmed
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 이미 끝 방향(Shooter=왼쪽, Partner=오른쪽)에 있으면 그 방향 버튼을 숨긴다.
	SetArrowButtonVisible(LeftButton, GuestState != ELobbyGuestWidgetState::Shooter);
	SetArrowButtonVisible(RightButton, GuestState != ELobbyGuestWidgetState::Partner);
}
