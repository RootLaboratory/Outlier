// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierMatchmakingSubsystem.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "GameFramework/Controller.h"

void UOutlierMatchmakingSubsystem::SetMatchmakingMode(EOutlierMatchmakingMode NewMode)
{
	MatchmakingMode = NewMode;
}

void UOutlierMatchmakingSubsystem::EnqueueForPairThenRolePick(AController* Controller)
{
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("[Matchmaking] EnqueueForPairThenRolePick: Controller is NULL"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] EnqueueForPairThenRolePick: %s added. WaitingPlayers before: %d"),
		*Controller->GetName(), WaitingPlayers.Num());

	Cancel(Controller);

	if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
	{
		PS->ClearPendingLobbyState();
	}

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	WaitingPlayers.Add(Request);

	UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] WaitingPlayers after enqueue: %d"), WaitingPlayers.Num());

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::EnqueueByRole(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller || DesiredRole == EOutlierPlayerRole::None)
	{
		return;
	}

	Cancel(Controller);

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.DesiredRole = DesiredRole;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (DesiredRole == EOutlierPlayerRole::Shooter)
	{
		WaitingShooters.Add(Request);
	}
	else if (DesiredRole == EOutlierPlayerRole::Partner)
	{
		WaitingPartners.Add(Request);
	}

	TryCreateMatch();
}

bool UOutlierMatchmakingSubsystem::SelectRoleInPendingMatch(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller ||
		(DesiredRole != EOutlierPlayerRole::None
			&& DesiredRole != EOutlierPlayerRole::Shooter
			&& DesiredRole != EOutlierPlayerRole::Partner))
	{
		return false;
	}

	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num(); ++MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		const bool bRoleTakenByOther =
			(DesiredRole == EOutlierPlayerRole::Shooter &&
				Match.ShooterController &&
				Match.ShooterController != Controller) ||
			(DesiredRole == EOutlierPlayerRole::Partner &&
				Match.PartnerController &&
				Match.PartnerController != Controller);

		if (bRoleTakenByOther)
		{
			return false;
		}

		if (Match.ShooterController == Controller)
		{
			Match.ShooterController = nullptr;
		}

		if (Match.PartnerController == Controller)
		{
			Match.PartnerController = nullptr;
		}

		if (DesiredRole == EOutlierPlayerRole::Shooter)
		{
			Match.ShooterController = Controller;
		}
		else if (DesiredRole == EOutlierPlayerRole::Partner)
		{
			Match.PartnerController = Controller;
		}

		if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
		{
			PS->SetPendingLobbyMatchId(Match.PendingMatchId);
			PS->SetPendingLobbyRole(DesiredRole);
		}

		if (Match.IsReady())
		{
			AController* ShooterController = Match.ShooterController;
			AController* PartnerController = Match.PartnerController;

			PendingRolePickMatches.RemoveAt(MatchIndex);

			CreateMatch(
				ShooterController,
				PartnerController,
				EOutlierPlayerRole::Shooter,
				EOutlierPlayerRole::Partner
			);
		}

		return true;
	}

	return false;
}

bool UOutlierMatchmakingSubsystem::TryStartPendingMatch(AController* Controller)
{
	if (!Controller)
	{
		return false;
	}

	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num(); ++MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		if (!Match.IsReady())
		{
			return false;
		}

		AController* ShooterController = Match.ShooterController;
		AController* PartnerController = Match.PartnerController;

		PendingRolePickMatches.RemoveAt(MatchIndex);

		CreateMatch(
			ShooterController,
			PartnerController,
			EOutlierPlayerRole::Shooter,
			EOutlierPlayerRole::Partner
		);

		return true;
	}

	return false;
}

void UOutlierMatchmakingSubsystem::Cancel(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
	{
		PS->ClearPendingLobbyState();
	}

	auto RemoveController = [Controller](TArray<FOutlierMatchRequest>& Queue)
	{
		Queue.RemoveAll([Controller](const FOutlierMatchRequest& Request)
		{
			return Request.Controller == Controller;
		});
	};

	RemoveController(WaitingPlayers);
	RemoveController(WaitingShooters);
	RemoveController(WaitingPartners);

	for (int32 MatchIndex = PendingRolePickMatches.Num() - 1; MatchIndex >= 0; --MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		AController* OtherController = Match.FirstController == Controller
			? Match.SecondController.Get()
			: Match.FirstController.Get();

		PendingRolePickMatches.RemoveAt(MatchIndex);

		if (OtherController)
		{
			int32 OtherSlotIndex = INDEX_NONE;
			if (AOutlierPlayerState* OtherPS = OtherController->GetPlayerState<AOutlierPlayerState>())
			{
				OtherSlotIndex = OtherPS->GetPendingLobbySlotIndex();
				OtherPS->ClearPendingLobbyState();
			}

			FOutlierMatchRequest Request;
			Request.Controller = OtherController;
			Request.PendingLobbySlotIndex = OtherSlotIndex;
			Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			WaitingPlayers.Add(Request);
		}
	}

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::ReleaseMatch(int32 PairId)
{
	if (PairId == INDEX_NONE)
	{
		return;
	}

	if (const int32* ArenaId = ActivePairArenaIds.Find(PairId))
	{
		if (UWorld* World = GetWorld())
		{
			if (UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
			{
				ArenaPool->ReleaseArena(*ArenaId);
			}
		}

		ActivePairArenaIds.Remove(PairId);
	}
}

void UOutlierMatchmakingSubsystem::TryCreateMatch()
{
	switch (MatchmakingMode)
	{
	case EOutlierMatchmakingMode::PairThenRolePick:
		TryCreatePairThenRolePickMatch();
		break;

	case EOutlierMatchmakingMode::RoleQueue:
		TryCreateRoleQueueMatch();
		break;

	default:
		break;
	}
}

void UOutlierMatchmakingSubsystem::TryCreatePairThenRolePickMatch()
{
	UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] TryCreatePairThenRolePickMatch: WaitingPlayers=%d"), WaitingPlayers.Num());

	while (WaitingPlayers.Num() >= 2)
	{
		FOutlierMatchRequest First = WaitingPlayers[0];
		FOutlierMatchRequest Second = WaitingPlayers[1];

		WaitingPlayers.RemoveAt(0, 2);

		if (!First.Controller || !Second.Controller)
		{
			UE_LOG(LogTemp, Error, TEXT("[Matchmaking] PairThenRolePick: one of the controllers is NULL, skipping"));
			continue;
		}

		FOutlierPendingRolePickMatch PendingMatch;
		PendingMatch.PendingMatchId = NextPendingMatchId++;
		PendingMatch.FirstController = First.Controller;
		PendingMatch.SecondController = Second.Controller;
		PendingMatch.CreatedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

		const auto IsValidLobbySlot = [](int32 SlotIndex)
		{
			return SlotIndex == 0 || SlotIndex == 1;
		};

		int32 FirstSlotIndex = 0;
		int32 SecondSlotIndex = 1;
		const bool bFirstHasPreferredSlot = IsValidLobbySlot(First.PendingLobbySlotIndex);
		const bool bSecondHasPreferredSlot = IsValidLobbySlot(Second.PendingLobbySlotIndex);
		if (bFirstHasPreferredSlot && bSecondHasPreferredSlot)
		{
			FirstSlotIndex = First.PendingLobbySlotIndex;
			SecondSlotIndex = First.PendingLobbySlotIndex != Second.PendingLobbySlotIndex
				? Second.PendingLobbySlotIndex
				: 1 - FirstSlotIndex;
		}
		else if (bFirstHasPreferredSlot)
		{
			FirstSlotIndex = First.PendingLobbySlotIndex;
			SecondSlotIndex = 1 - FirstSlotIndex;
		}
		else if (bSecondHasPreferredSlot)
		{
			SecondSlotIndex = Second.PendingLobbySlotIndex;
			FirstSlotIndex = 1 - SecondSlotIndex;
		}

		UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] PendingMatch created: Id=%d, First=%s, Second=%s"),
			PendingMatch.PendingMatchId, *First.Controller->GetName(), *Second.Controller->GetName());

		if (AOutlierPlayerState* FirstPS = First.Controller->GetPlayerState<AOutlierPlayerState>())
		{
			FirstPS->SetPendingLobbyMatchId(PendingMatch.PendingMatchId);
			FirstPS->SetPendingLobbySlotIndex(FirstSlotIndex);
			FirstPS->SetPendingLobbyRole(EOutlierPlayerRole::None);
			UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] FirstPS PendingLobbyMatchId set to %d"), PendingMatch.PendingMatchId);
		}

		if (AOutlierPlayerState* SecondPS = Second.Controller->GetPlayerState<AOutlierPlayerState>())
		{
			SecondPS->SetPendingLobbyMatchId(PendingMatch.PendingMatchId);
			SecondPS->SetPendingLobbySlotIndex(SecondSlotIndex);
			SecondPS->SetPendingLobbyRole(EOutlierPlayerRole::None);
			UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] SecondPS PendingLobbyMatchId set to %d"), PendingMatch.PendingMatchId);
		}

		PendingRolePickMatches.Add(PendingMatch);
	}
}

void UOutlierMatchmakingSubsystem::TryCreateRoleQueueMatch()
{
	if (WaitingShooters.Num() <= 0 || WaitingPartners.Num() <= 0)
	{
		return;
	}

	FOutlierMatchRequest Shooter = WaitingShooters[0];
	FOutlierMatchRequest Partner = WaitingPartners[0];

	WaitingShooters.RemoveAt(0);
	WaitingPartners.RemoveAt(0);

	CreateMatch(
		Shooter.Controller,
		Partner.Controller,
		EOutlierPlayerRole::Shooter,
		EOutlierPlayerRole::Partner
	);
}

void UOutlierMatchmakingSubsystem::CreateMatch(
	AController* FirstController,
	AController* SecondController,
	EOutlierPlayerRole FirstRole,
	EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UOutlierArenaPoolSubsystem* ArenaPool =
		World->GetSubsystem<UOutlierArenaPoolSubsystem>();

	if (!ArenaPool)
	{
		return;
	}

	FOutlierArenaInstance* Arena = ArenaPool->AcquireArena();
	if (!Arena)
	{
		return;
	}

	const int32 PairId = NextPairId++;

	AOutlierGameMode* GameMode = World->GetAuthGameMode<AOutlierGameMode>();
	if (!GameMode)
	{
		ArenaPool->ReleaseArena(Arena->ArenaId);
		return;
	}

	Arena->PairId = PairId;
	ActivePairArenaIds.Add(PairId, Arena->ArenaId);

	GameMode->StartMatchedPair(
		FirstController,
		SecondController,
		PairId,
		Arena->ArenaId,
		FirstRole,
		SecondRole
	);
}
