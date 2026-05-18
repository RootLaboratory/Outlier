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
	// 서버 끊기는 것도 고려해서 만들어야 함.
	// 기본적으로 Title 창이 나와야 겠죠
}

void ULobbyWidget::HandlePendingLobbyStateChanged(AOutlierPlayerState* ChangedPS)
{
	/*UE_LOG(LogTemp, Warning, TEXT("[LobbyWidget] HandlePendingLobbyStateChanged: PS=%s, MatchId=%d, Role=%d"),
		ChangedPS ? *ChangedPS->GetPlayerName() : TEXT("NULL"),
		ChangedPS ? ChangedPS->GetPendingLobbyMatchId() : -1,
		ChangedPS ? (int32)ChangedPS->GetPendingLobbyRole() : -1);*/

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

	/*UE_LOG(LogTemp, Warning,
		TEXT("[LobbyWidget] Refresh START Widget=%s PC=%s LocalPS=%s MatchId=%d GS=%s PlayerArray=%d ShooterImg=%s PartnerImg=%s StartBtn=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwningPlayer()),
		*GetNameSafe(LocalPS),
		PendingLobbyMatchId,
		*GetNameSafe(GS),
		GS ? GS->PlayerArray.Num() : -1,
		*GetNameSafe(ShooterSelectedImage),
		*GetNameSafe(PartnerSelectedImage),
		*GetNameSafe(StartButton)
	);

	UE_LOG(LogTemp, Warning,
		TEXT("[LobbyWidget] Refresh END this=%p NetMode=%d WidgetVis=%d IsVisible=%d ShooterTaken=%d PartnerTaken=%d ShooterImgVis=%d PartnerImgVis=%d StartEnabled=%d"),
		this,
		GetWorld() ? (int32)GetWorld()->GetNetMode() : -1,
		(int32)GetVisibility(),
		IsVisible(),
		bShooterTaken,
		bPartnerTaken,
		ShooterSelectedImage ? (int32)ShooterSelectedImage->GetVisibility() : -1,
		PartnerSelectedImage ? (int32)PartnerSelectedImage->GetVisibility() : -1,
		StartButton ? StartButton->GetIsEnabled() : 0);*/

	if (GS && PendingLobbyMatchId != INDEX_NONE)
	{
		for (APlayerState* RawPS : GS->PlayerArray)
		{
			const AOutlierPlayerState* OutlierPS = Cast<AOutlierPlayerState>(RawPS);

		/*	UE_LOG(LogTemp, Warning,
				TEXT("[LobbyWidget] PlayerArray PS=%s Cast=%s PSMatchId=%d PendingRole=%d FinalRole=%d IsLocalPS=%d"),
				*GetNameSafe(RawPS),
				*GetNameSafe(OutlierPS),
				OutlierPS ? OutlierPS->GetPendingLobbyMatchId() : INDEX_NONE,
				OutlierPS ? (int32)OutlierPS->GetPendingLobbyRole() : -1,
				OutlierPS ? (int32)OutlierPS->GetPlayerRole() : -1,
				OutlierPS == LocalPS
			);*/

			if (!OutlierPS || OutlierPS->GetPendingLobbyMatchId() != PendingLobbyMatchId)
			{
				continue;
			}

			bShooterTaken |= OutlierPS->GetPendingLobbyRole() == EOutlierPlayerRole::Shooter;
			bPartnerTaken |= OutlierPS->GetPendingLobbyRole() == EOutlierPlayerRole::Partner;
		}
	}

	/*UE_LOG(LogTemp, Warning,
		TEXT("[LobbyWidget] Refresh RESULT MatchId=%d ShooterTaken=%d PartnerTaken=%d"),
		PendingLobbyMatchId,
		bShooterTaken,
		bPartnerTaken
	);*/

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
