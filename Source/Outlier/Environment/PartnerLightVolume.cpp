#include "Environment/PartnerLightVolume.h"

#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "CollisionShape.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Editor.h"
#include "Engine/Polys.h"
#include "Model.h"
#endif

APartnerLightVolume::APartnerLightVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bGenerateOverlapEventsDuringLevelStreaming = true;
	if (UBrushComponent* BrushComp = GetBrushComponent())
	{
		BrushComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BrushComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		BrushComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		BrushComp->SetGenerateOverlapEvents(true);
		BrushComp->OnComponentBeginOverlap.AddDynamic(this, &APartnerLightVolume::HandleBeginOverlap);
		BrushComp->OnComponentEndOverlap.AddDynamic(this, &APartnerLightVolume::HandleEndOverlap);
	}
}

void APartnerLightVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!GIsEditor || !GEditor || IsTemplate() || !World || World->IsGameWorld() || HasBrushGeometry()) return;
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!HasBrushGeometry()) CreateDefaultBoxBrush();
	}));
#endif
}

#if WITH_EDITOR
bool APartnerLightVolume::HasBrushGeometry() const
{
	return Brush && Brush->Polys && Brush->Polys->Element.Num() > 0;
}

void APartnerLightVolume::CreateDefaultBoxBrush()
{
	if (!IsValid(this) || !GetWorld()) return;
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = DefaultBrushSize.X;
	Builder->Y = DefaultBrushSize.Y;
	Builder->Z = DefaultBrushSize.Z;
	UActorFactory::CreateBrushForVolumeActor(this, Builder);
}
#endif

void APartnerLightVolume::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
	SetActorTickEnabled(true);
	// Initial overlaps may precede either actor's BeginPlay. Reconcile after this initialization pass.
	GetWorldTimerManager().SetTimerForNextTick(this, &APartnerLightVolume::RefreshInitialOverlaps);
}

void APartnerLightVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bEndingPlay) return;
	// One volume drives the world-wide check to avoid repeating it for every volume.
	for (TActorIterator<APartnerLightVolume> It(GetWorld()); It; ++It)
	{
		if (It->bEndingPlay || !It->HasActorBegunPlay()) continue;
		if (*It != this) return;
		break;
	}
	for (TActorIterator<APartnerCharacter> It(GetWorld()); It; ++It)
	{
		RefreshPartnerLights(*It);
	}
}

void APartnerLightVolume::RefreshInitialOverlaps()
{
	if (bEndingPlay) return;
	GetBrushComponent()->UpdateOverlaps();
	for (TActorIterator<APartnerCharacter> It(GetWorld()); It; ++It)
	{
		RefreshPartnerLights(*It);
	}
}

void APartnerLightVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	for (TActorIterator<APartnerCharacter> It(GetWorld()); It; ++It)
	{
		RefreshPartnerLights(*It);
	}
	Super::EndPlay(EndPlayReason);
}

void APartnerLightVolume::RefreshPartnerLights(APartnerCharacter* Partner)
{
	if (!IsValid(Partner) || !Partner->HasActorBegunPlay() || Partner->IsActorBeingDestroyed()) return;

	bool bInsideLightVolume = false;
	const UCapsuleComponent* Capsule = Partner->GetCapsuleComponent();
	for (TActorIterator<APartnerLightVolume> It(Partner->GetWorld()); It; ++It)
	{
		if (It->bEndingPlay) continue;
		if (It->GetBrushComponent()->OverlapComponent(Capsule->GetComponentLocation(),
			Capsule->GetComponentQuat(), Capsule->GetCollisionShape()))
		{
			bInsideLightVolume = true;
			break;
		}
	}

	TInlineComponentArray<USpotLightComponent*> Lights(Partner);
	for (USpotLightComponent* Light : Lights)
	{
		if (Light->IsVisible() != bInsideLightVolume)
		{
			Light->SetVisibility(bInsideLightVolume);
		}
	}
}

void APartnerLightVolume::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bEndingPlay) RefreshPartnerLights(Cast<APartnerCharacter>(OtherActor));
}

void APartnerLightVolume::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!bEndingPlay) RefreshPartnerLights(Cast<APartnerCharacter>(OtherActor));
}
