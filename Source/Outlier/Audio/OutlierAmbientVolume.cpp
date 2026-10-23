#include "Audio/OutlierAmbientVolume.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/BrushComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Outlier.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Editor.h"
#include "Engine/Polys.h"
#include "Model.h"
#endif

AOutlierAmbientVolume::AOutlierAmbientVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	// 로컬 Pawn 의 출입만 감지하면 되므로 Trigger 프로필로 Overlap 만 받는다.
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionProfileName(TEXT("Trigger"));
		BrushComp->SetGenerateOverlapEvents(true);
	}
}

void AOutlierAmbientVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	// Place Actors 의 Volumes 로 놓으면 Volume 팩토리가 Brush 를 만든다.
	// BP 를 콘텐츠 브라우저에서 드래그하면 팩토리를 거치지 않아 Brush 가 비어 있으므로 다음 에디터 틱에 Box 를 만든다.
	const UWorld* World = GetWorld();
	if (!GIsEditor || !GEditor || IsTemplate() || !World || World->IsGameWorld() || HasBrushGeometry()) return;
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!HasBrushGeometry()) CreateDefaultBoxBrush();
	}));
#endif
}

#if WITH_EDITOR
bool AOutlierAmbientVolume::HasBrushGeometry() const
{
	return Brush && Brush->Polys && Brush->Polys->Element.Num() > 0;
}

void AOutlierAmbientVolume::CreateDefaultBoxBrush()
{
	if (!IsValid(this) || !GetWorld()) return;
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = DefaultBrushSize.X;
	Builder->Y = DefaultBrushSize.Y;
	Builder->Z = DefaultBrushSize.Z;
	UActorFactory::CreateBrushForVolumeActor(this, Builder);
}
#endif

void AOutlierAmbientVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<APawn>& Pawn : OverlappingPawns)
	{
		if (Pawn.IsValid())
		{
			Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
		}
	}
	OverlappingPawns.Reset();
	SetPlaying(false);

	Super::EndPlay(EndPlayReason);
}

void AOutlierAmbientVolume::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		TrackPawn(Pawn);
	}
}

void AOutlierAmbientVolume::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);
	if (APawn* Pawn = Cast<APawn>(OtherActor))
	{
		UntrackPawn(Pawn);
	}
}

void AOutlierAmbientVolume::HandlePawnControllerChanged(
	APawn* Pawn,
	AController* OldController,
	AController* NewController)
{
	RefreshPlayback();
}

void AOutlierAmbientVolume::TrackPawn(APawn* Pawn)
{
	if (OverlappingPawns.Contains(Pawn))
	{
		return;
	}

	OverlappingPawns.Add(Pawn);
	// 스폰 직후엔 아직 빙의 전이라 IsLocallyControlled 가 false 일 수 있어, 빙의/해제 시점에 다시 판단한다.
	Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnControllerChanged);
	RefreshPlayback();
}

void AOutlierAmbientVolume::UntrackPawn(APawn* Pawn)
{
	OverlappingPawns.Remove(Pawn);
	Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
	RefreshPlayback();
}

void AOutlierAmbientVolume::RefreshPlayback()
{
	OverlappingPawns.RemoveAll(
		[](const TWeakObjectPtr<APawn>& Pawn)
		{
			return !Pawn.IsValid();
		});

	SetPlaying(OverlappingPawns.ContainsByPredicate(
		[](const TWeakObjectPtr<APawn>& Pawn)
		{
			return Pawn->IsLocallyControlled();
		}));
}

void AOutlierAmbientVolume::SetPlaying(bool bNewPlaying)
{
	if (bPlaying == bNewPlaying)
	{
		return;
	}

	bPlaying = bNewPlaying;
	ApplyPlayback();
}

void AOutlierAmbientVolume::ApplyPlayback()
{
	GetWorldTimerManager().ClearTimer(ResolveRetryTimer);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UOutlierAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<UOutlierAudioSubsystem>() : nullptr;
	if (!Audio)
	{
		return;
	}

	if (!ResolvedType.IsValid())
	{
		// 아직 시작한 적이 없으니 멈출 것도 없다.
		if (!bPlaying)
		{
			return;
		}

		bool bPending = false;
		if (!Audio->ResolveTypeForContext(ContextTag, ResolvedType, bPending))
		{
			if (bPending)
			{
				// Bank 내용이 올라오는 중이다. 잠시 뒤 다시 찾는다.
				GetWorldTimerManager().SetTimer(ResolveRetryTimer, this, &ThisClass::ApplyPlayback, 0.2f, false);
			}
			else
			{
				UE_LOG(LogOutlier, Warning,
					TEXT("[Audio] %s: ContextTag '%s' is not in any Audio Bank."),
					*GetName(),
					*ContextTag.ToString());
			}
			return;
		}
	}

	if (ResolvedType == FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.BGM")))
	{
		Audio->SetZoneMusic(this, ContextTag, bPlaying, FadeInDuration, FadeOutDuration);
	}
	else
	{
		Audio->SetLocalEmitterLoop(this, ResolvedType, ContextTag, bPlaying, bTwoD, bPlaying ? 0.0f : FadeOutDuration);
	}
}
