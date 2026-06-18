#include "UI/LobbyWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "OutlierPlayerState.h"
#include "FrontendPlayerController.h"

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

	if (ShooterSelect)
	{
		ShooterSelect->OnClicked.AddDynamic(this, &ULobbyWidget::HandleShooterSelectClicked);
	}

	if (PartnerSelect)
	{
		PartnerSelect->OnClicked.AddDynamic(this, &ULobbyWidget::HandlePartnerSelectClicked);
	}

	if (StartButton)
	{
		StartButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleStartButtonClicked);
	}

	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &ULobbyWidget::HandleBackButtonEvent);
	}

	BindLobbyPlayerStateDelegates();
	RefreshRoleSelection();
	StartLobbyRefreshTimer();
}

void ULobbyWidget::NativeDestruct()
{
	StopLobbyRefreshTimer();
	UnbindLobbyPlayerStateDelegates();
	Super::NativeDestruct();

}

void ULobbyWidget::HandleShooterSelectClicked()
{
	RequestRole(EOutlierPlayerRole::Shooter);
}

void ULobbyWidget::HandlePartnerSelectClicked()
{
	RequestRole(EOutlierPlayerRole::Partner);
}

void ULobbyWidget::HandleStartButtonClicked()
{
	AFrontendPlayerController* FrontendPC = Cast<AFrontendPlayerController>(GetOwningPlayer());
	if (!FrontendPC)
	{
		return;
	}

	FrontendPC->RequestStartPendingMatch();
}

void ULobbyWidget::HandleBackButtonEvent()
{
	OnBackRequested.Broadcast();
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

	AFrontendPlayerController* PC = Cast<AFrontendPlayerController>(GetOwningPlayer());


	//UE_LOG(LogTemp, Warning,
	//	TEXT("[LobbyUI] RequestRole Widget=%s PC=%s Local=%d Auth=%d Role=%d"),
	//	*GetNameSafe(this),
	//	*GetNameSafe(PC),
	//	PC ? PC->IsLocalController() : 0,
	//	PC ? PC->HasAuthority() : 0,
	//	(int32)DesiredRole);


	FrontendPC->RequestSelectLobbyRole(DesiredRole);
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

	bool bShooterTaken = false;
	bool bPartnerTaken = false;

	const AOutlierPlayerState* LocalPS = GetLocalOutlierPlayerState();
	const int32 PendingLobbyMatchId = LocalPS
		? LocalPS->GetPendingLobbyMatchId()
		: INDEX_NONE;

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;

	if (GS && PendingLobbyMatchId != INDEX_NONE)
	{
		for (APlayerState* RawPS : GS->PlayerArray)
		{
			const AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS);

			if (!OutlierPS || OutlierPS->GetPendingLobbyMatchId() != PendingLobbyMatchId)
			{
				continue;
			}

			bShooterTaken |= OutlierPS->GetPendingLobbyRole() == EOutlierPlayerRole::Shooter;
			bPartnerTaken |= OutlierPS->GetPendingLobbyRole() == EOutlierPlayerRole::Partner;
		}
	}

	if (ShooterSelectedImage)
	{
		ShooterSelectedImage->SetVisibility(
			bShooterTaken ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden
		);
	}

	if (PartnerSelectedImage)
	{
		PartnerSelectedImage->SetVisibility(
			bPartnerTaken ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden
		);
	}

	if (StartButton)
	{
		StartButton->SetIsEnabled(bShooterTaken && bPartnerTaken);
	}
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
