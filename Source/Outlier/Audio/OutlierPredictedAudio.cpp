#include "Audio/OutlierAudioSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Shooter/ShooterCharacter.h"

UOutlierAudioSubsystem::FMovementAudioRecord& UOutlierAudioSubsystem::BeginMovementAudio(
	const FMovementAudioKey& Key, bool bServerOwned)
{
	// 시간 창으로 이벤트를 추측하지 않고 Pawn+회차 키를 보관한다. 개수를 제한해 오래된 대기도 만료시킨다.
	if (!MovementAudioRecords.Contains(Key))
	{
		constexpr int32 MaxMovementAudioRecords = 128;
		while (MovementAudioOrder.Num() >= MaxMovementAudioRecords)
		{
			MovementAudioRecords.Remove(MovementAudioOrder[0]);
			MovementAudioOrder.RemoveAt(0);
		}
		MovementAudioOrder.Add(Key);
	}
	FMovementAudioRecord& Record = MovementAudioRecords.FindOrAdd(Key);
	Record.Token = ++NextMovementPlaybackToken;
	Record.State = EMovementAudioState::Pending;
	Record.bServerOwned = bServerOwned;
	return Record;
}

bool UOutlierAudioSubsystem::IsMovementPlaybackCurrent(AActor* Emitter, uint64 EventId, uint64 Token) const
{
	if (Token == 0) return true;
	const APawn* Pawn = Cast<APawn>(Emitter);
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Emitter);
	if (!IsValid(Pawn) || !Pawn->IsLocallyControlled() || Pawn->IsActorBeingDestroyed()
		|| (Shooter && Shooter->IsDead())) return false;
	const FMovementAudioRecord* Record = MovementAudioRecords.Find({ Emitter, EventId });
	return Record && Record->Token == Token && Record->State == EMovementAudioState::Pending;
}

void UOutlierAudioSubsystem::FinishMovementPlayback(AActor* Emitter, uint64 EventId, uint64 Token, bool bPlayed)
{
	if (Token == 0) return;
	FMovementAudioRecord* Record = MovementAudioRecords.Find({ Emitter, EventId });
	if (Record && Record->Token == Token)
	{
		Record->State = bPlayed ? EMovementAudioState::Played : EMovementAudioState::Failed;
	}
}

bool UOutlierAudioSubsystem::PlayMovementSurfaceAudio(AActor* Emitter, FGameplayTag Context,
	EPhysicalSurface Surface, const FVector& Location, uint64 EventId)
{
	APawn* Pawn = Cast<APawn>(Emitter);
	if (!IsValid(Pawn) || Emitter->GetWorld() != GetWorld()) return false;
	FOutlierAudioPlayRequest Request;
	Request.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Player"));
	Request.EmitterActor = Emitter;
	Request.MovementAudioEventId = EventId;
	Request.Location = Location;
	Request.bHasLocation = true;
	Request.bExactContexts = true;
	Request.OriginWorld = GetWorld();
	GetSurfaceContextCandidates(Context, Surface, Request.OrderedContexts);
	if (Emitter->HasAuthority())
	{
		// Listen 소유자도 이 한 경로만 통과한다. 별도의 선재생을 요청하지 않는다.
		return PlayRelevantAtLocationFromServer(Request);
	}
	if (!Pawn->IsLocallyControlled() || EventId == 0) return false;
	const FMovementAudioKey Key { Emitter, EventId };
	if (MovementAudioRecords.Contains(Key)) return true;
	Request.MovementPlaybackToken = BeginMovementAudio(Key, false).Token;
	const bool bAccepted = PlayLocalAtLocation(Request);
	if (!bAccepted) FinishMovementPlayback(Emitter, EventId, Request.MovementPlaybackToken, false);
	return bAccepted;
}

bool UOutlierAudioSubsystem::PrepareDeliveredMovementAudio(FOutlierResolvedAudioPlay& Play)
{
	if (Play.MovementAudioEventId == 0) return true;
	if (Play.MovementPlaybackToken != 0)
	{
		// Bank/음원 대기 뒤 재진입한 요청은 자신의 토큰이 아직 유효할 때만 계속한다.
		return IsMovementPlaybackCurrent(Play.MovementAudioEmitter, Play.MovementAudioEventId, Play.MovementPlaybackToken);
	}
	const APawn* Pawn = Cast<APawn>(Play.MovementAudioEmitter);
	if (!Pawn) return false;
	if (!Pawn->IsLocallyControlled()) return true;
	const FMovementAudioKey Key { Play.MovementAudioEmitter, Play.MovementAudioEventId };
	if (const FMovementAudioRecord* Existing = MovementAudioRecords.Find(Key))
	{
		if (Existing->State == EMovementAudioState::Played || Existing->bServerOwned) return false;
	}
	// 실제 재생에 성공한 로컬음만 서버음을 생략한다. 대기/실패했다면 서버 요청이 토큰을 교체한다.
	Play.MovementPlaybackToken = BeginMovementAudio(Key, true).Token;
	return true;
}

void UOutlierAudioSubsystem::ClearPredictedMovementAudio(AActor* Emitter)
{
	// 에셋 로드는 다른 요청과 공유하므로 유지하고, 이 Pawn의 이동음 대기 항목만 제거한다.
	const auto IsClearedRequest = [Emitter](const FOutlierAudioPlayRequest& Request)
	{
		return Request.MovementAudioEventId != 0 && (!Emitter || Request.EmitterActor == Emitter);
	};
	for (auto& Pair : PendingPlaysByType)
		Pair.Value.RemoveAll([&](const auto& Item) { return IsClearedRequest(Item.Key); });
	for (auto& Pair : PendingSurfaceSelectionsBySound)
		Pair.Value.RemoveAll([&](const auto& Item) { return IsClearedRequest(Item.Key); });
	for (auto& Pair : PendingResolvedPlaysByType)
		Pair.Value.RemoveAll([Emitter](const auto& Item)
		{
			return Item.Key.MovementAudioEventId != 0 && (!Emitter || Item.Key.MovementAudioEmitter == Emitter);
		});
	for (auto& Pair : PendingPlaysBySound)
		Pair.Value.RemoveAll([Emitter](const FPendingPlay& Item)
		{
			return Item.MovementAudioEventId != 0 && (!Emitter || Item.MovementAudioEmitter.Get() == Emitter);
		});
	for (int32 Index = MovementAudioOrder.Num() - 1; Index >= 0; --Index)
	{
		if (!Emitter || MovementAudioOrder[Index].Emitter == TWeakObjectPtr<AActor>(Emitter))
		{
			MovementAudioRecords.Remove(MovementAudioOrder[Index]);
			MovementAudioOrder.RemoveAt(Index);
		}
	}
}
