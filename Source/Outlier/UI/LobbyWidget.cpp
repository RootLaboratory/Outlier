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
}

void ULobbyWidget::NativeDestruct()
{
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
