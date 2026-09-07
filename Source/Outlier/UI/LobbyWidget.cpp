#include "UI/LobbyWidget.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "OutlierPlayerState.h"
#include "FrontendPlayerController.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

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
	if (Guest1Widget)
	{
		Guest1Widget->SetGuestIndex(0);
	}
	if (Guest2Widget)
	{
		Guest2Widget->SetGuestIndex(1);
	}
	RefreshRoleSelection();
	StartLobbyRefreshTimer();
}

void ULobbyWidget::NativeDestruct()
{
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
	if (const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState())
	{
		LocalGuestPreviewState = ConvertRoleToGuestState(LocalPS->GetPendingLobbyRole());
		bLocalClearConfirmationPreview = false;
	}

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
				ConvertRoleToGuestState(OutlierPS->GetPendingLobbyRole());
			bGuestConfirmed[OutlierPS->GetPendingLobbySlotIndex()] =
				OutlierPS->GetPendingLobbyRole() != EOutlierPlayerRole::None;
		}
	}

	if (FMath::IsWithin(LocalSlotIndex, 0, 2) && LocalPS)
	{
		GuestStates[LocalSlotIndex] = LocalGuestPreviewState;
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
}

void ULobbyWidget::SetLocalGuestPreviewState(ELobbyGuestWidgetState NewState)
{
	if (LocalGuestPreviewState == NewState)
	{
		return;
	}

	LocalGuestPreviewState = NewState;
	RefreshGuestWidgets();
}

void ULobbyWidget::MoveLocalGuestPreview(int32 Direction)
{
	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	if (LocalPS && LocalPS->GetPendingLobbyRole() != EOutlierPlayerRole::None)
	{
		return;
	}

	if (Direction < 0)
	{
		SetLocalGuestPreviewState(
			LocalGuestPreviewState == ELobbyGuestWidgetState::Shooter
				? ELobbyGuestWidgetState::Default
				: ELobbyGuestWidgetState::Shooter);
		return;
	}

	if (Direction > 0)
	{
		SetLocalGuestPreviewState(
			LocalGuestPreviewState == ELobbyGuestWidgetState::Partner
				? ELobbyGuestWidgetState::Default
				: ELobbyGuestWidgetState::Partner);
	}
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
	Transform.Translation.X = 0.0f;
	if (State == ELobbyGuestWidgetState::Shooter)
	{
		Transform.Translation.X = -GetRoleOffsetPixels();
	}
	else if (State == ELobbyGuestWidgetState::Partner)
	{
		Transform.Translation.X = GetRoleOffsetPixels();
	}

	GuestWidget->SetRenderTransform(Transform);
	GuestWidget->SetGuestIndex(GuestIndex);
	GuestWidget->SetGuestState(State, bIsLocalGuest);
	GuestWidget->SetConfirmed(bIsConfirmed);
	OnGuestWidgetStateChanged(GuestIndex, State, bIsLocalGuest);
}

float ULobbyWidget::GetRoleOffsetPixels() const
{
	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (GEngine && GetWorld())
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	return ViewportSize.X * RoleOffsetViewportScale;
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
