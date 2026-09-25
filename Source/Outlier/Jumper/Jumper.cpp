#include "Jumper/Jumper.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameplayTags/OutlierGameplayTags.h"

AJumper::AJumper()
{
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	JumperMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JumperMesh"));
	JumperMesh->SetupAttachment(RootComponent);
	JumperMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JumperMesh->SetGenerateOverlapEvents(false);

	HackTargetCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("HackTargetCollision"));
	HackTargetCollision->SetupAttachment(RootComponent);
	HackTargetCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HackTargetCollision->SetCollisionObjectType(ECC_WorldDynamic);
	HackTargetCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	HackTargetCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	HackTargetCollision->SetGenerateOverlapEvents(false);

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::Jump());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Jumper());

	JumperVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("JumperVolume"));
	JumperVolume->SetupAttachment(RootComponent);
	JumperVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JumperVolume->SetCollisionObjectType(ECC_WorldDynamic);
	JumperVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	JumperVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	JumperVolume->SetGenerateOverlapEvents(true);
	JumperVolume->OnComponentBeginOverlap.AddDynamic(this, &AJumper::HandleVolumeBeginOverlap);
	JumperVolume->OnComponentEndOverlap.AddDynamic(this, &AJumper::HandleVolumeEndOverlap);
}

void AJumper::BeginPlay()
{
	Super::BeginPlay();

	SetJumperActive(bEnableVolumeOnBeginPlay ||
		(HackableComponent && HackableComponent->HasHackTag(OutlierGameplayTags::State::HackedOnce())));
}

void AJumper::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetJumperActive(false);
	Super::EndPlay(EndPlayReason);
}

UHackableComponent* AJumper::GetHackableComponent() const
{
	return HackableComponent;
}

void AJumper::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (bDebugJumper)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[JumperDebug] HackEffect Jumper=%s Effect=%s Result=%d Authority=%d"),
			*GetNameSafe(this),
			*EffectTag.ToString(),
			static_cast<int32>(Context.Result),
			HasAuthority() ? 1 : 0);
	}

	if (Context.Result == EHackResult::Success && EffectTag == HackGameplayTags::Effect::Jumper())
	{
		SetJumperActive(true);
	}
}

void AJumper::SetJumperActive(bool bActive)
{
	if (bJumperActive == bActive)
	{
		return;
	}

	bJumperActive = bActive;
	if (bDebugJumper)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[JumperDebug] ActiveChanged Jumper=%s Active=%d EffectId=%s Direction=%s"),
			*GetNameSafe(this),
			bJumperActive ? 1 : 0,
			*EffectId.ToString(),
			*GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal().ToCompactString());
	}

	if (JumperVolume)
	{
		JumperVolume->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

		// If a character was already inside the volume when hacking completed,
		// no new BeginOverlap event is guaranteed. Synchronize the current set.
		if (bActive && HasAuthority())
		{
			TArray<AActor*> CurrentOverlaps;
			JumperVolume->GetOverlappingActors(CurrentOverlaps);
			for (AActor* Actor : CurrentOverlaps)
			{
				HandleVolumeBeginOverlap(nullptr, Actor, nullptr, INDEX_NONE, false, FHitResult());
			}
		}
	}

	if (!bActive)
	{
		for (const TWeakObjectPtr<AActor>& ActorPtr : OverlappingAffectableActors)
		{
			if (AActor* Actor = ActorPtr.Get())
			{
				if (IJumperAffectableInterface* Affectable = Cast<IJumperAffectableInterface>(Actor))
				{
					Affectable->EndJumperEffect(EffectId, this);
				}
			}
		}
		OverlappingAffectableActors.Reset();
	}
}

FJumperMovementEffect AJumper::BuildMovementEffect() const
{
	FJumperMovementEffect Effect;
	Effect.Direction = GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
	Effect.AscendMultiplier = AscendMultiplier;
	Effect.DescendMultiplier = DescendMultiplier;
	Effect.PartnerFlightMultiplier = PartnerFlightMultiplier;
	return Effect;
}

void AJumper::HandleVolumeBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bJumperActive || !OtherActor || OtherActor == this)
	{
		return;
	}

	IJumperAffectableInterface* Affectable = Cast<IJumperAffectableInterface>(OtherActor);
	if (!Affectable || OverlappingAffectableActors.Contains(OtherActor))
	{
		return;
	}

	OverlappingAffectableActors.Add(OtherActor);
	if (bDebugJumper)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[JumperDebug] BeginOverlap Jumper=%s Other=%s Interface=%d EffectId=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OtherActor),
			OtherActor->GetClass()->ImplementsInterface(UJumperAffectableInterface::StaticClass()) ? 1 : 0,
			*EffectId.ToString());
	}
	Affectable->BeginJumperEffect(EffectId, BuildMovementEffect(), this);
}

void AJumper::HandleVolumeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!HasAuthority() || !OtherActor || !OverlappingAffectableActors.Contains(OtherActor))
	{
		return;
	}

	OverlappingAffectableActors.Remove(OtherActor);
	if (bDebugJumper)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[JumperDebug] EndOverlap Jumper=%s Other=%s EffectId=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OtherActor),
			*EffectId.ToString());
	}

	if (IJumperAffectableInterface* Affectable = Cast<IJumperAffectableInterface>(OtherActor))
	{
		Affectable->EndJumperEffect(EffectId, this);
	}
}
