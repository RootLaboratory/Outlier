#include "UI/LobbyWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "OutlierPlayerState.h"
#include "FrontendPlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

void ULobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 코드가 브러시를 바꾸기 전, 디자이너 원본을 한 번만 저장한다.
	CacheDefaultRoleImageBrushes();
}

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	/*UE_LOG(LogTemp, Warning,
		TEXT("[LobbyWidget] NativeConstruct this=%p World=%s NetMode=%d OwningPC=%s Local=%d Auth=%d InViewport=%d Visibility=%d"),
		this,
		*GetNameSafe(GetWorld()),
		GetWorld() ? (int32)GetWorld()->GetNetMode() : -1,
		*GetNameSafe(GetOwningPlayer()),
		GetOwningPlayer() ? GetOwningPlayer()->IsLocalController() : 0,
		GetOwningPlayer() ? GetOwningPlayer()->HasAuthority() : 0,
		IsInViewport(),
		(int32)GetVisibility());*/

	BindLobbyPlayerStateDelegates();
	// 레이어 재표시로 Construct가 다시 불리면 현재 포커스를 다시 적용한다.
	bLocalPreviewEdited = false;
	bLocalClearConfirmationPreview = false;
	if (const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState())
	{
		LocalPreviewMatchId = LocalPS->GetPendingLobbyMatchId();
		LocalGuestPreviewState = ConvertRoleToGuestState(LocalPS->GetPendingLobbyPreviewRole());
	}
	bRoleFocusApplied = false;
	if (Guest1Widget)
	{
		Guest1Widget->SetGuestIndex(0);
		Guest1Widget->OnMoveRequested.RemoveAll(this);
		Guest1Widget->OnMoveRequested.AddUObject(this, &ThisClass::MoveLocalGuestPreview);
	}
	if (Guest2Widget)
	{
		Guest2Widget->SetGuestIndex(1);
		Guest2Widget->OnMoveRequested.RemoveAll(this);
		Guest2Widget->OnMoveRequested.AddUObject(this, &ThisClass::MoveLocalGuestPreview);
	}
	RefreshRoleSelection();
	StartLobbyRefreshTimer();
}

void ULobbyWidget::NativeDestruct()
{
	if (Guest1Widget) Guest1Widget->OnMoveRequested.RemoveAll(this);
	if (Guest2Widget) Guest2Widget->OnMoveRequested.RemoveAll(this);
	StopLobbyRefreshTimer();
	UnbindLobbyPlayerStateDelegates();
	Super::NativeDestruct();

}

void ULobbyWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	BindLobbyPlayerStateDelegates();
	RefreshRoleSelection();
}

bool ULobbyWidget::HandleUILayerEscape_Implementation()
{
	if (!TryClearLocalGuestConfirmation())
	{
		OnBackRequested.Broadcast();
		RequestCancelMatchmaking();
		PopSelfFromLayer();
	}
	return true;
}

bool ULobbyWidget::HandleUILayerConfirmed_Implementation()
{
	TryConfirmLocalGuestPreview();
	return true;
}

bool ULobbyWidget::HandleUILayerUp_Implementation()
{
	return false;
}

bool ULobbyWidget::HandleUILayerDown_Implementation()
{
	return false;
}

bool ULobbyWidget::HandleUILayerLeft_Implementation()
{
	MoveLocalGuestPreview(-1);
	return true;
}

bool ULobbyWidget::HandleUILayerRight_Implementation()
{
	MoveLocalGuestPreview(1);
	return true;
}

void ULobbyWidget::HandlePendingLobbyStateChanged(AOutlierPlayerState* ChangedPS)
{
	BindLobbyPlayerStateDelegates();
	RefreshRoleSelection();
}

void ULobbyWidget::RequestRole(EOutlierPlayerRole DesiredRole)
{
	AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (!FrontendPC)
	{
		return;
	}

	//UE_LOG(LogTemp, Warning,
	//	TEXT("[LobbyUI] RequestRole Widget=%s PC=%s Local=%d Auth=%d Role=%d"),
	//	*GetNameSafe(this),
	//	*GetNameSafe(PC),
	//	PC ? PC->IsLocalController() : 0,
	//	PC ? PC->HasAuthority() : 0,
	//	(int32)DesiredRole);


	FrontendPC->RequestSelectLobbyRole(DesiredRole);
}

void ULobbyWidget::RequestCancelMatchmaking()
{
	AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (FrontendPC)
	{
		FrontendPC->RequestCancelMatchmaking();
	}
}

void ULobbyWidget::PopSelfFromLayer()
{
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Menu.Back"));
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (LayerSubsystem)
	{
		LayerSubsystem->PopWidget(this);
	}
}

void ULobbyWidget::BindLobbyPlayerStateDelegates()
{
	AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	if (LocalPS)
	{
		LocalPS->OnPendingLobbyStateChanged.RemoveAll(this);
		LocalPS->OnPendingLobbyStateChanged.AddUObject(
			this,
			&ULobbyWidget::HandlePendingLobbyStateChanged
		);
	}

	const int32 PendingLobbyMatchId = LocalPS
		? LocalPS->GetPendingLobbyMatchId()
		: INDEX_NONE;

	if (PendingLobbyMatchId == INDEX_NONE)
	{
		return;
	}

	AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}

	for (APlayerState* RawPS : GS->PlayerArray)
	{
		AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS);
		if (!OutlierPS || OutlierPS->GetPendingLobbyMatchId() != PendingLobbyMatchId)
		{
			continue;
		}

		OutlierPS->OnPendingLobbyStateChanged.RemoveAll(this);
		OutlierPS->OnPendingLobbyStateChanged.AddUObject(
			this,
			&ULobbyWidget::HandlePendingLobbyStateChanged
		);
	}
}

void ULobbyWidget::UnbindLobbyPlayerStateDelegates()
{
	if (AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState())
	{
		LocalPS->OnPendingLobbyStateChanged.RemoveAll(this);
	}

	AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}

	for (APlayerState* RawPS : GS->PlayerArray)
	{
		if (AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS))
		{
			OutlierPS->OnPendingLobbyStateChanged.RemoveAll(this);
		}
	}
}

AOutlierPlayerState* ULobbyWidget::GetLocalOutlierPlayerState() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? PC->GetPlayerState<AOutlierPlayerState>() : nullptr;
}

void ULobbyWidget::RefreshRoleSelection()
{
	BindLobbyPlayerStateDelegates();

	RefreshGuestWidgets();
}

void ULobbyWidget::RefreshGuestWidgets()
{
	ELobbyGuestWidgetState GuestStates[2] =
	{
		ELobbyGuestWidgetState::Default,
		ELobbyGuestWidgetState::Default
	};
	bool bGuestConfirmed[2] =
	{
		false,
		false
	};

	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	const int32 LocalSlotIndex = LocalPS ? LocalPS->GetPendingLobbySlotIndex() : INDEX_NONE;
	if (LocalPS)
	{
		// Polling also initializes widgets whose PlayerState replicated before delegate binding.
		if (LocalPreviewMatchId != LocalPS->GetPendingLobbyMatchId())
		{
			LocalPreviewMatchId = LocalPS->GetPendingLobbyMatchId();
			bLocalPreviewEdited = false;
			bLocalClearConfirmationPreview = false;
		}
		// Another guest's update or an older preview echo must not rewind local input.
		if (!bLocalPreviewEdited)
		{
			LocalGuestPreviewState = ConvertRoleToGuestState(LocalPS->GetPendingLobbyPreviewRole());
		}
		if (LocalPS->GetPendingLobbyRole() == EOutlierPlayerRole::None)
		{
			bLocalClearConfirmationPreview = false;
		}
	}
	const int32 PendingLobbyMatchId = LocalPS
		? LocalPS->GetPendingLobbyMatchId()
		: INDEX_NONE;

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GS && PendingLobbyMatchId != INDEX_NONE)
	{
		for (APlayerState* RawPS : GS->PlayerArray)
		{
			const AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS);
			if (!OutlierPS
				|| OutlierPS->GetPendingLobbyMatchId() != PendingLobbyMatchId
				|| !FMath::IsWithin(OutlierPS->GetPendingLobbySlotIndex(), 0, 2))
			{
				continue;
			}

			GuestStates[OutlierPS->GetPendingLobbySlotIndex()] =
				ConvertRoleToGuestState(OutlierPS->GetPendingLobbyRole() != EOutlierPlayerRole::None
					? OutlierPS->GetPendingLobbyRole() : OutlierPS->GetPendingLobbyPreviewRole());
			bGuestConfirmed[OutlierPS->GetPendingLobbySlotIndex()] =
				OutlierPS->GetPendingLobbyRole() != EOutlierPlayerRole::None;
		}
	}

	if (FMath::IsWithin(LocalSlotIndex, 0, 2) && LocalPS)
	{
		GuestStates[LocalSlotIndex] = LocalPS->GetPendingLobbyRole() != EOutlierPlayerRole::None
			? ConvertRoleToGuestState(LocalPS->GetPendingLobbyRole()) : LocalGuestPreviewState;
		if (bLocalClearConfirmationPreview)
		{
			GuestStates[LocalSlotIndex] = ELobbyGuestWidgetState::Default;
			bGuestConfirmed[LocalSlotIndex] = false;
		}
	}

	for (int32 GuestIndex = 0; GuestIndex < 2; ++GuestIndex)
	{
		ApplyGuestWidgetState(
			GuestIndex,
			GuestStates[GuestIndex],
			GuestIndex == LocalSlotIndex,
			bGuestConfirmed[GuestIndex]);
	}

	// Guest 위젯과 같은 GuestStates(로컬 미리보기 + 상대의 복제된 역할)로 역할 이미지를 맞춘다.
	RefreshRoleFocusImages(GuestStates);
}

void ULobbyWidget::CacheDefaultRoleImageBrushes()
{
	if (bRoleImageBrushesCached)
	{
		return;
	}

	if (ShooterLobbyImage)
	{
		DefaultShooterLobbyBrush = ShooterLobbyImage->GetBrush();
		UE_CLOG(!DefaultShooterLobbyBrush.GetResourceObject(), LogTemp, Warning,
			TEXT("[LobbyWidget] ShooterLobbyImage has no designer image. Unfocused state will draw a blank brush."));
	}
	if (PartnerLobbyImage)
	{
		DefaultPartnerLobbyBrush = PartnerLobbyImage->GetBrush();
		UE_CLOG(!DefaultPartnerLobbyBrush.GetResourceObject(), LogTemp, Warning,
			TEXT("[LobbyWidget] PartnerLobbyImage has no designer image. Unfocused state will draw a blank brush."));
	}
	bRoleImageBrushesCached = true;
}

void ULobbyWidget::RefreshRoleFocusImages(const ELobbyGuestWidgetState (&GuestStates)[2])
{
	const bool bShooterFocused = GuestStates[0] == ELobbyGuestWidgetState::Shooter
		|| GuestStates[1] == ELobbyGuestWidgetState::Shooter;
	const bool bPartnerFocused = GuestStates[0] == ELobbyGuestWidgetState::Partner
		|| GuestStates[1] == ELobbyGuestWidgetState::Partner;

	if (bRoleFocusApplied
		&& bAppliedShooterFocused == bShooterFocused
		&& bAppliedPartnerFocused == bPartnerFocused)
	{
		return;
	}

	bRoleFocusApplied = true;
	bAppliedShooterFocused = bShooterFocused;
	bAppliedPartnerFocused = bPartnerFocused;
	ApplyRoleFocusImages(bShooterFocused, bPartnerFocused);
	OnRoleFocusChanged(bShooterFocused, bPartnerFocused);
}

void ULobbyWidget::ApplyRoleFocusImages_Implementation(bool bShooterFocused, bool bPartnerFocused)
{
	SetRoleImageFocused(ShooterLobbyImage, DefaultShooterLobbyBrush, ShooterFocusedImage, bShooterFocused);
	SetRoleImageFocused(PartnerLobbyImage, DefaultPartnerLobbyBrush, PartnerFocusedImage, bPartnerFocused);
}

void ULobbyWidget::SetRoleImageFocused(
	UImage* RoleImage,
	const FSlateBrush& DefaultBrush,
	UTexture2D* FocusedTexture,
	bool bFocused)
{
	if (!RoleImage)
	{
		return;
	}

	if (!bFocused || !FocusedTexture)
	{
		RoleImage->SetBrush(DefaultBrush);
		return;
	}

	// 크기·Tint·DrawAs 등 디자이너 설정은 유지하고 리소스만 바꾼다.
	FSlateBrush FocusedBrush = DefaultBrush;
	FocusedBrush.SetResourceObject(FocusedTexture);
	RoleImage->SetBrush(FocusedBrush);
}

void ULobbyWidget::SetLocalGuestPreviewState(ELobbyGuestWidgetState NewState)
{
	if (LocalGuestPreviewState == NewState)
	{
		return;
	}

	LocalGuestPreviewState = NewState;
	bLocalPreviewEdited = true;
	if (AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer()))
	{
		FrontendPC->RequestPreviewLobbyRole(ConvertGuestStateToRole(NewState));
	}
	RefreshGuestWidgets();
}

void ULobbyWidget::MoveLocalGuestPreview(int32 Direction)
{
	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	if (!LocalPS || LocalPS->GetPendingLobbyMatchId() == INDEX_NONE
		|| LocalPS->GetPendingLobbyRole() != EOutlierPlayerRole::None)
	{
		return;
	}

	const ELobbyGuestWidgetState PreviousState = LocalGuestPreviewState;
	if (Direction < 0)
	{
		SetLocalGuestPreviewState(
			LocalGuestPreviewState == ELobbyGuestWidgetState::Shooter
				? ELobbyGuestWidgetState::Default
				: ELobbyGuestWidgetState::Shooter);
	}
	else if (Direction > 0)
	{
		SetLocalGuestPreviewState(
			LocalGuestPreviewState == ELobbyGuestWidgetState::Partner
				? ELobbyGuestWidgetState::Default
				: ELobbyGuestWidgetState::Partner);
	}

	if (LocalGuestPreviewState != PreviousState) OutlierAudioFeedback::MenuNavigate(this);
}

bool ULobbyWidget::TryConfirmLocalGuestPreview()
{
	const EOutlierPlayerRole DesiredRole = ConvertGuestStateToRole(LocalGuestPreviewState);
	if (DesiredRole == EOutlierPlayerRole::None)
	{
		return false;
	}

	if (IsRoleTakenByOther(DesiredRole))
	{
		OnLobbyRoleConfirmRejected(DesiredRole);
		return false;
	}

	bLocalClearConfirmationPreview = false;
	RequestRole(DesiredRole);
	return true;
}

bool ULobbyWidget::TryClearLocalGuestConfirmation()
{
	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	if (!LocalPS || LocalPS->GetPendingLobbyRole() == EOutlierPlayerRole::None)
	{
		return false;
	}

	LocalGuestPreviewState = ELobbyGuestWidgetState::Default;
	bLocalPreviewEdited = true;
	bLocalClearConfirmationPreview = true;
	RequestRole(EOutlierPlayerRole::None);
	RefreshGuestWidgets();
	return true;
}

bool ULobbyWidget::IsRoleTakenByOther(EOutlierPlayerRole DesiredRole) const
{
	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	const int32 PendingLobbyMatchId = LocalPS
		? LocalPS->GetPendingLobbyMatchId()
		: INDEX_NONE;
	if (!LocalPS || PendingLobbyMatchId == INDEX_NONE || DesiredRole == EOutlierPlayerRole::None)
	{
		return false;
	}

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return false;
	}

	for (APlayerState* RawPS : GS->PlayerArray)
	{
		const AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS);
		if (OutlierPS
			&& OutlierPS != LocalPS
			&& OutlierPS->GetPendingLobbyMatchId() == PendingLobbyMatchId
			&& OutlierPS->GetPendingLobbyRole() == DesiredRole)
		{
			return true;
		}
	}

	return false;
}

EOutlierPlayerRole ULobbyWidget::ConvertGuestStateToRole(ELobbyGuestWidgetState State) const
{
	switch (State)
	{
	case ELobbyGuestWidgetState::Shooter:
		return EOutlierPlayerRole::Shooter;

	case ELobbyGuestWidgetState::Partner:
		return EOutlierPlayerRole::Partner;

	default:
		return EOutlierPlayerRole::None;
	}
}

ELobbyGuestWidgetState ULobbyWidget::ConvertRoleToGuestState(EOutlierPlayerRole Role) const
{
	switch (Role)
	{
	case EOutlierPlayerRole::Shooter:
		return ELobbyGuestWidgetState::Shooter;

	case EOutlierPlayerRole::Partner:
		return ELobbyGuestWidgetState::Partner;

	default:
		return ELobbyGuestWidgetState::Default;
	}
}

ULobbyGuestWidget* ULobbyWidget::GetGuestWidgetByIndex(int32 GuestIndex) const
{
	if (GuestIndex == 0)
	{
		return Guest1Widget;
	}

	if (GuestIndex == 1)
	{
		return Guest2Widget;
	}

	return nullptr;
}

void ULobbyWidget::ApplyGuestWidgetState(
	int32 GuestIndex,
	ELobbyGuestWidgetState State,
	bool bIsLocalGuest,
	bool bIsConfirmed)
{
	ULobbyGuestWidget* GuestWidget = GetGuestWidgetByIndex(GuestIndex);
	if (!GuestWidget)
	{
		return;
	}

	FWidgetTransform Transform = GuestWidget->GetRenderTransform();
	Transform.Translation = FVector2D::ZeroVector;
	if (State == ELobbyGuestWidgetState::Shooter)
	{
		Transform.Translation = GetRoleOffset(GuestIndex);
	}
	else if (State == ELobbyGuestWidgetState::Partner)
	{
		Transform.Translation = GetRoleOffset(GuestIndex);
		Transform.Translation.X *= -1.0f;
	}

	GuestWidget->SetRenderTransform(Transform);
	GuestWidget->SetGuestIndex(GuestIndex);
	GuestWidget->SetGuestState(State, bIsLocalGuest);
	GuestWidget->SetConfirmed(bIsConfirmed);
	OnGuestWidgetStateChanged(GuestIndex, State, bIsLocalGuest);
}

FVector2D ULobbyWidget::GetRoleOffset(int32 GuestIndex) const
{
	const ULobbyGuestWidget* GuestWidget = GetGuestWidgetByIndex(GuestIndex);
	const UWidget* LeftTarget = GuestIndex == 0 ? GuestTargetUp.Get() : GuestTargetDown.Get();
	if (!GuestWidget || !LeftTarget || GuestWidget->GetParent() != LeftTarget->GetParent())
	{
		return FVector2D::ZeroVector;
	}

	const UCanvasPanelSlot* GuestSlot = Cast<UCanvasPanelSlot>(GuestWidget->Slot);
	const UCanvasPanelSlot* TargetSlot = Cast<UCanvasPanelSlot>(LeftTarget->Slot);
	if (!GuestSlot || !TargetSlot)
	{
		return FVector2D::ZeroVector;
	}

	const FAnchors GuestAnchors = GuestSlot->GetAnchors();
	const FAnchors TargetAnchors = TargetSlot->GetAnchors();
	if (!GuestAnchors.Minimum.Equals(TargetAnchors.Minimum)
		|| !GuestAnchors.Maximum.Equals(TargetAnchors.Maximum)
		|| !GuestAnchors.Minimum.Equals(GuestAnchors.Maximum))
	{
		return FVector2D::ZeroVector;
	}

	FVector2D FrameCenter;
	if (!GuestWidget->GetFrameCenterInLocalSpace(FrameCenter))
	{
		return FVector2D::ZeroVector;
	}

	// 기본 슬롯 위치에서 계산하여 반복 갱신 시 Render Translation이 누적되지 않도록 한다.
	const FVector2D DefaultFrameCenter = GuestSlot->GetPosition()
		- GuestSlot->GetAlignment() * GuestWidget->GetCachedGeometry().GetLocalSize() + FrameCenter;
	const FVector2D TargetSize = TargetSlot->GetAutoSize() ? LeftTarget->GetDesiredSize() : TargetSlot->GetSize();
	const FVector2D TargetCenter = TargetSlot->GetPosition()
		+ (FVector2D(0.5f, 0.5f) - TargetSlot->GetAlignment()) * TargetSize;
	return TargetCenter - DefaultFrameCenter;
}

void ULobbyWidget::StartLobbyRefreshTimer()
{
	if (!GetWorld())
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		LobbyRefreshTimerHandle,
		this,
		&ULobbyWidget::RefreshRoleSelection,
		0.1f,
		true
	);
}

void ULobbyWidget::StopLobbyRefreshTimer()
{
	if (!GetWorld())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(LobbyRefreshTimerHandle);
}
