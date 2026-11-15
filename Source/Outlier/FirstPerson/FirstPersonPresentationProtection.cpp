#include "FirstPerson/FirstPersonPlayerController.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"

bool AFirstPersonPlayerController::BeginPresentationProtection(const FGuid& ExecutionId,
	AFirstPersonCharacter* Participant, AFirstPersonPlayerController* Peer)
{
	if (!HasAuthority() || !ExecutionId.IsValid() || !IsValid(Participant)
		|| GetPawn() != Participant || Participant->GetWorld() != GetWorld()
		|| (Peer && (Peer == this || Peer->GetWorld() != GetWorld()))) return false;
	if (const FPresentationProtectionRequest* Existing = PresentationProtectionRequests.Find(ExecutionId))
	{
		// cleanup에 들어간 실행은 재시도 실행이 아니다. 옛 ACK와 새 시작이 같은 ID를 공유하지 않는다.
		return !Existing->bRecoveryRequested && Existing->Participant == Participant && Existing->Peer.Get() == Peer
			&& (!PresentationProtectionArena.IsValid()
				|| Existing->Generation == PresentationProtectionArena->GetGameplayGeneration());
	}
	if (!Participant->AcquirePresentationProtection(this, ExecutionId)) return false;
	PresentationProtectionArena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	FPresentationProtectionRequest& Request = PresentationProtectionRequests.Add(ExecutionId);
	Request.Participant = Participant;
	Request.Peer = Peer;
	Request.Generation = PresentationProtectionArena.IsValid() ? PresentationProtectionArena->GetGameplayGeneration() : 0;
	Request.Deadline = FPlatformTime::Seconds() + 180.0;
	if (!PresentationProtectionWatchdog.IsValid())
	{
		// 월드 pause와 시간 배율에 영향받지 않는 기존 Controller의 core ticker 패턴을 재사용한다.
		PresentationProtectionWatchdog = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickPresentationProtection), 0.25f);
	}
	return true;
}

bool AFirstPersonPlayerController::IsPresentationProtectionCurrent(const FGuid& ExecutionId, const APawn* Participant) const
{
	const FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(ExecutionId);
	return HasAuthority() && Request && !Request->bRecoveryRequested && IsValid(Participant)
		&& Request->Participant.Get() == Participant && GetPawn() == Participant
		&& (!PresentationProtectionArena.IsValid()
			|| Request->Generation == PresentationProtectionArena->GetGameplayGeneration());
}

void AFirstPersonPlayerController::ReleasePresentationProtection(const FGuid& ExecutionId)
{
	if (!HasAuthority()) return;
	FPresentationProtectionRequest Request;
	if (!PresentationProtectionRequests.RemoveAndCopyValue(ExecutionId, Request)) return;
	if (AFirstPersonCharacter* Participant = Request.Participant.Get())
	{
		Participant->ReleasePresentationProtection(this, ExecutionId);
	}
}

void AFirstPersonPlayerController::AwaitPresentationProtectionRestore(const FGuid& ExecutionId, float MinimumDuration)
{
	if (!HasAuthority() || !FMath::IsFinite(MinimumDuration) || MinimumDuration < 0.0f) return;
	if (FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(ExecutionId))
	{
		if (Request->bAwaitingRestore) return;
		Request->bAwaitingRestore = true;
		Request->MinimumFinishTime = FPlatformTime::Seconds() + MinimumDuration;
		Request->Deadline = Request->MinimumFinishTime + 10.0;
	}
}

void AFirstPersonPlayerController::RecoverPresentationProtection(const FGuid& ExecutionId, float CleanupRevealDuration, bool bRevealScreen)
{
	if (!HasAuthority() || !FMath::IsFinite(CleanupRevealDuration) || CleanupRevealDuration < 0.0f) return;
	FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(ExecutionId);
	if (!Request || Request->bRecoveryRequested) return;
	if (PresentationProtectionArena.IsValid()
		&& Request->Generation != PresentationProtectionArena->GetGameplayGeneration())
	{
		// 구세대 취소는 새 로딩/인트로 화면을 걷어내지 않는다.
		ReleasePresentationProtection(ExecutionId);
		return;
	}
	// 정상 복구 대기 중 취소될 수도 있다. 옛 ACK를 버리고 cleanup 완료를 새로 확인한다.
	Request->bAwaitingRestore = false;
	Request->bRestored = false;
	Request->bRecoveryRequested = true;
	AwaitPresentationProtectionRestore(ExecutionId, CleanupRevealDuration);
	ClientRecoverPresentationProtection(ExecutionId, Request->Participant.Get(), CleanupRevealDuration, bRevealScreen);
}

void AFirstPersonPlayerController::ClientRecoverPresentationProtection_Implementation(
	FGuid ExecutionId, APawn* Participant, float Duration, bool bRevealScreen)
{
	if (!IsLocalController() || !ExecutionId.IsValid() || !FMath::IsFinite(Duration) || Duration < 0.0f
		|| !IsValid(Participant) || GetPawn() != Participant) return;
	if (LocalSuitTransitionId == ExecutionId) ClearLocalSuitTransition();
	if (LocalElevatorExecutionId == ExecutionId) ClearLocalElevatorFade();
	ULocalPlayerPostProcessSubsystem* PP = GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerPostProcessSubsystem>() : nullptr;
	if (!PP) return;
	PresentationRecoveryPostProcess = PP;
	if (!bRevealScreen)
	{
		// 정상 화면 복귀는 다시 fade를 시작하지 않는다. 다른 연출이 덮고 있으면 그대로 기다린다.
		QueuePresentationRestore(ExecutionId, Participant);
		return;
	}
	if (FDelegateHandle* Previous = PresentationRecoveryCallbacks.Find(ExecutionId))
	{
		PP->OnScreenBlackoutRevealed.Remove(*Previous);
	}
	// 콜백은 Controller에 남는다. 통로/엘리베이터 Actor가 사라져도 이 완료 통보는 유효하다.
	PresentationRecoveryCallbacks.Add(ExecutionId,
		PP->OnScreenBlackoutRevealed.AddUObject(this, &ThisClass::HandlePresentationRecoveryFinished,
			ExecutionId, TWeakObjectPtr<APawn>(Participant)));
	PP->StartScreenBlackoutReveal(Duration);
}

void AFirstPersonPlayerController::HandlePresentationRecoveryFinished(
	FGuid ExecutionId, TWeakObjectPtr<APawn> Participant)
{
	FDelegateHandle Callback;
	if (!PresentationRecoveryCallbacks.RemoveAndCopyValue(ExecutionId, Callback)) return;
	if (ULocalPlayerPostProcessSubsystem* PP = PresentationRecoveryPostProcess.Get())
	{
		PP->OnScreenBlackoutRevealed.Remove(Callback);
	}
	QueuePresentationRestore(ExecutionId, Participant);
}

void AFirstPersonPlayerController::QueuePresentationRestore(FGuid ExecutionId, TWeakObjectPtr<APawn> Participant)
{
	PresentationRestoredParticipants.Add(ExecutionId, {Participant, FPlatformTime::Seconds() + 10.0});
	if (!PresentationRestoreWatchdog.IsValid())
	{
		// 화면 콜백보다 Pawn의 입력 차단 해제 복제가 늦을 수 있다. 활성 복구 동안만 확인한다.
		PresentationRestoreWatchdog = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickPresentationRestore), 0.05f);
	}
}

bool AFirstPersonPlayerController::TickPresentationRestore(float DeltaTime)
{
	(void)DeltaTime;
	TArray<FGuid> Finished;
	for (const auto& Entry : PresentationRestoredParticipants)
	{
		AFirstPersonCharacter* Participant = Cast<AFirstPersonCharacter>(Entry.Value.Participant.Get());
		if (!Participant || GetPawn() != Participant)
		{
			Finished.Add(Entry.Key);
			continue;
		}
		if (FPlatformTime::Seconds() >= Entry.Value.Deadline)
		{
			// ACK를 위조해 보호를 풀지 않는다. 서버 watchdog의 동일한 실패 복귀로 수렴한다.
			Finished.Add(Entry.Key);
			continue;
		}
		const ULocalPlayerPostProcessSubsystem* PP = PresentationRecoveryPostProcess.Get();
		if (Participant->IsSuitTransitionBlocked() || !PP || PP->GetScreenBlackoutAlpha() > KINDA_SMALL_NUMBER) continue;
		Finished.Add(Entry.Key);
		ServerNotifyPresentationRestored(Entry.Key, Participant, true);
	}
	for (const FGuid& Id : Finished) PresentationRestoredParticipants.Remove(Id);
	if (!PresentationRestoredParticipants.IsEmpty()) return true;
	PresentationRestoreWatchdog.Reset();
	return false;
}

void AFirstPersonPlayerController::ServerNotifyPresentationRestored_Implementation(FGuid ExecutionId, APawn* Participant, bool bRecovery)
{
	FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(ExecutionId);
	if (!HasAuthority() || !Request || !Request->bAwaitingRestore || Request->bRestored
		|| Request->bRecoveryRequested != bRecovery
		|| !IsValid(Participant) || Request->Participant.Get() != Participant || GetPawn() != Participant
		|| (PresentationProtectionArena.IsValid()
			&& Request->Generation != PresentationProtectionArena->GetGameplayGeneration())) return;
	Request->bRestored = true;
	TryFinishPresentationProtection(ExecutionId);
}

void AFirstPersonPlayerController::TryFinishPresentationProtection(const FGuid& ExecutionId)
{
	const FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(ExecutionId);
	if (!Request || !Request->bRestored || FPlatformTime::Seconds() < Request->MinimumFinishTime) return;
	AFirstPersonPlayerController* Peer = Request->Peer.Get();
	if (Peer)
	{
		const FPresentationProtectionRequest* Other = Peer->PresentationProtectionRequests.Find(ExecutionId);
		if (Other && (Other->Peer.Get() != this || !Other->bRestored
			|| Other->bRecoveryRequested != Request->bRecoveryRequested
			|| FPlatformTime::Seconds() < Other->MinimumFinishTime)) return;
	}
	// 어느 한쪽 화면만 먼저 밝아져도 둘 다 보호한다. 마지막 ACK에서 양쪽을 함께 해제한다.
	ReleasePresentationProtection(ExecutionId);
	if (Peer) Peer->ReleasePresentationProtection(ExecutionId);
}

bool AFirstPersonPlayerController::TickPresentationProtection(float DeltaTime)
{
	(void)DeltaTime;
	TArray<FGuid> Ids;
	PresentationProtectionRequests.GetKeys(Ids);
	for (const FGuid& Id : Ids)
	{
		const FPresentationProtectionRequest* Request = PresentationProtectionRequests.Find(Id);
		if (!Request) continue;
		const bool bOldGeneration = PresentationProtectionArena.IsValid()
			&& Request->Generation != PresentationProtectionArena->GetGameplayGeneration();
		if (!Request->Participant.IsValid() || GetPawn() != Request->Participant.Get() || bOldGeneration)
		{
			AFirstPersonPlayerController* Peer = Request->Peer.Get();
			ReleasePresentationProtection(Id);
			if (Peer)
			{
				if (bOldGeneration) Peer->ReleasePresentationProtection(Id);
				else Peer->RecoverPresentationProtection(Id, 0.1f);
			}
			continue;
		}
		if (FPlatformTime::Seconds() >= Request->Deadline)
		{
			// 복구에 실패한 페어를 검은 화면/영구 보호 상태로 두지 않고 기존 매치 종료로 인계한다.
			UE_LOG(LogTemp, Error, TEXT("[PresentationProtection] Recovery timeout Controller=%s Execution=%s"),
				*GetName(), *Id.ToString());
			AFirstPersonPlayerController* Peer = Request->Peer.Get();
			PresentationProtectionWatchdog.Reset();
			AOutlierGameMode* Mode = GetWorld()->GetAuthGameMode<AOutlierGameMode>();
			if (!Mode || !Mode->CompleteArenaMatch())
			{
				ClientArenaRecoveryFailed(false, TEXT("Presentation recovery timed out."));
				if (Peer) Peer->ClientArenaRecoveryFailed(false, TEXT("Presentation recovery timed out."));
			}
			return false;
		}
		TryFinishPresentationProtection(Id);
	}
	if (!PresentationProtectionRequests.IsEmpty()) return true;
	PresentationProtectionWatchdog.Reset();
	return false;
}

void AFirstPersonPlayerController::ClearPresentationProtection()
{
	if (PresentationRestoreWatchdog.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PresentationRestoreWatchdog);
		PresentationRestoreWatchdog.Reset();
	}
	PresentationRestoredParticipants.Reset();
	if (PresentationProtectionWatchdog.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PresentationProtectionWatchdog);
		PresentationProtectionWatchdog.Reset();
	}
	TArray<FGuid> Ids;
	PresentationProtectionRequests.GetKeys(Ids);
	for (const FGuid& Id : Ids)
	{
		AFirstPersonPlayerController* Peer = PresentationProtectionRequests.FindChecked(Id).Peer.Get();
		ReleasePresentationProtection(Id);
		if (Peer) Peer->RecoverPresentationProtection(Id, 0.1f);
	}
	if (ULocalPlayerPostProcessSubsystem* PP = PresentationRecoveryPostProcess.Get())
	{
		for (const auto& Entry : PresentationRecoveryCallbacks) PP->OnScreenBlackoutRevealed.Remove(Entry.Value);
	}
	PresentationRecoveryCallbacks.Reset();
	PresentationRecoveryPostProcess.Reset();
}
