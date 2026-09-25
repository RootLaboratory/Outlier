#include "Drone/Partner/MagneticGeneratorTower.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Enemy/EnemyBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Outlier.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "Team/OutlierTeamIds.h"

AMagneticGeneratorTower::AMagneticGeneratorTower()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(false);
	bReplicates = true;

	HackableComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackableComponent"));
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::Magnetic());
	// 효과가 끝나면 다시 해킹해 재사용하는 타워다. HackedOnce 이후에도 후보가 되도록 명시한다.
	HackableComponent->HackTags.AddTag(HackGameplayTags::Use::Multiple());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::Magnetic());

	RootMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RootMesh"));
	SetRootComponent(RootMesh);
	RootMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	SphereVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereVisual"));
	SphereVisual->SetupAttachment(RootMesh);
	SphereVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereVisual->SetGenerateOverlapEvents(false);
	SphereVisual->SetVisibility(false);

	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->SetupAttachment(RootMesh);
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereCollision->SetCollisionObjectType(ECC_WorldDynamic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	SphereCollision->SetGenerateOverlapEvents(true);
	SphereCollision->OnComponentBeginOverlap.AddDynamic(this, &AMagneticGeneratorTower::HandleSphereBeginOverlap);
	SphereCollision->OnComponentEndOverlap.AddDynamic(this, &AMagneticGeneratorTower::HandleSphereEndOverlap);
}

void AMagneticGeneratorTower::BeginPlay()
{
	Super::BeginPlay();
	UpdateSphereCollisionTransform();
	UpdateVisualization();

	// RootMesh 스태틱 메시에 들어 있는 머티리얼을 슬롯별로 MID 로 감싼다.
	if (RootMesh)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < RootMesh->GetNumMaterials(); ++MaterialIndex)
		{
			if (UMaterialInstanceDynamic* MID = RootMesh->CreateDynamicMaterialInstance(MaterialIndex))
			{
				RootMeshMIDs.Add(MID);
			}
		}
	}
	// 활성화 전에는 꺼진 상태(0)로 둔다.
	ApplyRootMeshIntensity(0.0f);
}

void AMagneticGeneratorTower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 활성 중 파괴/언로드되면 Tick 만료 경로를 타지 못한다. 여기서 놓지 않으면 끌던 적이 Impact 상태에 갇힌다.
	StopAttraction();

	Super::EndPlay(EndPlayReason);
}

void AMagneticGeneratorTower::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (RemainingDuration <= 0.0f)
	{
		SetActorTickEnabled(false);
		return;
	}

	UpdateAttraction(DeltaSeconds);
	UpdateRootMeshIntensity(DeltaSeconds);
	// StopAttraction 은 남은 시간이 있어야 활성 상태로 보고 렌즈를 끄므로, 깎기 전에 만료를 판정한다.
	if (RemainingDuration <= DeltaSeconds)
	{
		StopAttraction();
		return;
	}
	RemainingDuration -= DeltaSeconds;
}

UHackableComponent* AMagneticGeneratorTower::GetHackableComponent() const
{
	return HackableComponent;
}

void AMagneticGeneratorTower::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[MagneticGeneratorHackDebug] HandleHackEffect Tower=%s Effect=%s Result=%d Authority=%s NetMode=%d Instigator=%s Target=%s"),
		*GetNameSafe(this),
		*EffectTag.ToString(),
		static_cast<int32>(Context.Result),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		*GetNameSafe(Context.InstigatorActor),
		*GetNameSafe(Context.TargetActor));

	if (Context.Result == EHackResult::Success && EffectTag == HackGameplayTags::Effect::Magnetic())
	{
		StartAttraction();
	}
}

void AMagneticGeneratorTower::SetSphereRadius(float NewRadius)
{
	SphereRadius = FMath::Max(NewRadius, 0.0f);
	UpdateSphereCollisionTransform();
	UpdateVisualization();
}

void AMagneticGeneratorTower::StartAttraction()
{
	// 아래에서 추적 목록을 비우므로, 끌던 적을 놓아야 하는 경우는 그 전에 정리한다.
	if (Duration <= 0.0f)
	{
		StopAttraction();
		return;
	}

	// 활성 중 재해킹되면 끌던 적은 그대로 두고 Duration 만 새로 시작한다. 겹친 적은 아래에서 다시 모은다.
	RemainingDuration = Duration;
	IntensityElapsed = 0.0f;
	OverlappingEnemies.Reset();
	PreviousEnemyLocations.Reset();
	EnemyPullVelocities.Reset();
	UpdateSphereCollisionTransform();

	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[MagneticGeneratorHackDebug] StartAttraction Tower=%s Authority=%s Duration=%.2f Radius=%.2f Power=%.2f SphereOrigin=%s"),
		*GetNameSafe(this),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		RemainingDuration,
		SphereRadius,
		Power,
		*(SphereVisual ? SphereVisual->GetComponentLocation().ToCompactString() : GetActorLocation().ToCompactString()));

	// 겹친 적 판정은 서버만 한다. 클라이언트는 아래 연출만 처리한다.
	if (HasAuthority() && SphereCollision)
	{
		SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		TArray<AActor*> CurrentOverlaps;
		SphereCollision->GetOverlappingActors(CurrentOverlaps, AEnemyBase::StaticClass());
		for (AActor* Actor : CurrentOverlaps)
		{
			if (AEnemyBase* Enemy = Cast<AEnemyBase>(Actor))
			{
				OverlappingEnemies.Add(Enemy);
			}
		}
	}

	SetActorTickEnabled(true);

	const FVector Origin = SphereVisual ? SphereVisual->GetComponentLocation() : GetActorLocation();
	StartLensPostProcess(Origin, SphereRadius, RemainingDuration);

	UpdateVisualization();
	// 활성화 중 재해킹되어도 이전 값이 한 프레임 비치지 않도록 0 에서 다시 시작시킨다.
	UpdateRootMeshIntensity(0.0f);
}

void AMagneticGeneratorTower::StopAttraction()
{
	// 렌즈 볼륨은 전역 하나라 이 타워가 켜 둔 경우에만 끈다.
	const bool bWasActive = RemainingDuration > 0.0f;
	RemainingDuration = 0.0f;
	IntensityElapsed = 0.0f;
	SetActorTickEnabled(false);

	if (HasAuthority())
	{
		for (const TWeakObjectPtr<AEnemyBase>& EnemyPtr : OverlappingEnemies)
		{
			if (AEnemyBase* Enemy = EnemyPtr.Get())
			{
				Enemy->EndMagneticPull(this);
			}
		}
	}
	OverlappingEnemies.Reset();
	PreviousEnemyLocations.Reset();
	EnemyPullVelocities.Reset();

	// 콜리전을 끄면 같은 적들의 EndOverlap 이 다시 오지만, 위에서 이미 놓았으므로 아무 일도 하지 않는다.
	if (SphereCollision)
	{
		SphereCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UpdateVisualization();
	ApplyRootMeshIntensity(0.0f);

	if (bWasActive)
	{
		EndLensPostProcess();
	}
}

void AMagneticGeneratorTower::StartLensPostProcess(FVector Origin, float Radius, float InDuration)
{
	UWorld* World = GetWorld();
	if (!World || Radius <= 0.0f || InDuration <= 0.0f)
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->StartMagneticPostProcess(Origin, Radius, InDuration);
	}
}

void AMagneticGeneratorTower::EndLensPostProcess()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PostProcessSubsystem = World->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		PostProcessSubsystem->EndMagneticPostProcess();
	}
}

void AMagneticGeneratorTower::UpdateAttraction(float DeltaSeconds)
{
	if (!HasAuthority() || SphereRadius <= 0.0f || Power <= 0.0f)
	{
		return;
	}

	const FVector Origin = SphereVisual ? SphereVisual->GetComponentLocation() : GetActorLocation();
	int32 ValidEnemyCount = 0;
	int32 AttractedEnemyCount = 0;

	/*UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[MagneticGeneratorAttractionDebug] Tick Tower=%s Remaining=%.3f Overlapping=%d Origin=%s"),
		*GetNameSafe(this),
		RemainingDuration,
		OverlappingEnemies.Num(),
		*Origin.ToCompactString());*/

	for (auto It = OverlappingEnemies.CreateIterator(); It; ++It)
	{
		AEnemyBase* Enemy = It->Get();
		if (!IsValid(Enemy) || Enemy->IsDead())
		{
			PreviousEnemyLocations.Remove(*It);
			EnemyPullVelocities.Remove(*It);
			It.RemoveCurrent();
			continue;
		}

		// The generator affects hostile enemies only; possessed/hacked enemies are excluded.
		if (Enemy->GetGenericTeamId().GetId() != OutlierTeamIds::Enemy)
		{
			EnemyPullVelocities.Remove(Enemy);
			continue;
		}

		++ValidEnemyCount;
		const FVector CurrentLocation = Enemy->GetActorLocation();
		const FVector PreviousLocation = PreviousEnemyLocations.FindRef(Enemy);
		const bool bHasPreviousLocation = PreviousEnemyLocations.Contains(Enemy);
		const FVector LocationDelta = bHasPreviousLocation
			? CurrentLocation - PreviousLocation
			: FVector::ZeroVector;
		PreviousEnemyLocations.FindOrAdd(Enemy) = CurrentLocation;

		const FVector TowardTower = Origin - Enemy->GetActorLocation();
		if (TowardTower.IsNearlyZero())
		{
			continue;
		}

		const FVector TargetPullVelocity = TowardTower.GetSafeNormal() * Power;
		FVector* PullVelocityPtr = EnemyPullVelocities.Find(Enemy);
		if (!PullVelocityPtr)
		{
			// 처음 잡힌 적은 자기 현재 속도에서 출발해 타워 쪽으로 휘어 들어온다.
			PullVelocityPtr = &EnemyPullVelocities.Add(Enemy, Enemy->GetVelocity());
		}
		FVector& PullVelocity = *PullVelocityPtr;
		PullVelocity = FMath::VInterpTo(PullVelocity, TargetPullVelocity, DeltaSeconds, PullInterpSpeed);
		Enemy->ApplyMagneticPullVelocity(PullVelocity, this);
		++AttractedEnemyCount;

		if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
		{
		/*	UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[MagneticGeneratorAttractionDebug] Enemy=%s Pos=%s Delta=%s Direction=%s TargetPullVelocity=%s PullVelocity=%s Velocity=%s"),
				*GetNameSafe(Enemy),
				*CurrentLocation.ToCompactString(),
				*LocationDelta.ToCompactString(),
				*TowardTower.GetSafeNormal().ToCompactString(),
				*TargetPullVelocity.ToCompactString(),
				*PullVelocity.ToCompactString(),
				*Movement->Velocity.ToCompactString());*/
		}
	}

	//UE_LOG(
	//	LogOutlier,
	//	Warning,
	//	TEXT("[MagneticGeneratorAttractionDebug] TickResult ValidEnemies=%d AttractedEnemies=%d"),
	//	ValidEnemyCount,
	//	AttractedEnemyCount);
}

void AMagneticGeneratorTower::UpdateSphereCollisionTransform()
{
	if (!SphereCollision)
	{
		return;
	}

	if (SphereVisual)
	{
		SphereCollision->SetRelativeLocation(SphereVisual->GetRelativeLocation());
	}
	SphereCollision->SetSphereRadius(FMath::Max(SphereRadius, 0.0f), false);
}

void AMagneticGeneratorTower::HandleSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(OtherActor))
	{
		OverlappingEnemies.Add(Enemy);
	}
}

void AMagneticGeneratorTower::HandleSphereEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (AEnemyBase* Enemy = Cast<AEnemyBase>(OtherActor))
	{
		if (HasAuthority())
		{
			Enemy->EndMagneticPull(this);
		}
		OverlappingEnemies.Remove(Enemy);
		PreviousEnemyLocations.Remove(Enemy);
		EnemyPullVelocities.Remove(Enemy);
	}
}

void AMagneticGeneratorTower::UpdateVisualization()
{
	if (!SphereVisual)
	{
		return;
	}

	if (SphereMesh)
	{
		SphereVisual->SetStaticMesh(SphereMesh);
	}
	SphereVisual->SetVisibility(bVisualizeSphere && RemainingDuration > 0.0f);
	SphereVisual->SetRelativeScale3D(FVector(SphereRadius / 50.0f));

	if (SphereMaterial)
	{
		if (!SphereMID)
		{
			SphereMID = UMaterialInstanceDynamic::Create(SphereMaterial, this);
		}
		SphereVisual->SetMaterial(0, SphereMID);
	}

	if (SphereMID)
	{
		SphereMID->SetScalarParameterValue(TEXT("SphereRadius"), SphereRadius);
	}
}

void AMagneticGeneratorTower::UpdateRootMeshIntensity(float DeltaSeconds)
{
	IntensityElapsed += DeltaSeconds;

	float Intensity;
	if (IntensityElapsed < IntensityRampUpTime)
	{
		// 활성화 직후 0 -> 1.
		Intensity = IntensityElapsed / IntensityRampUpTime;
	}
	else
	{
		// 이후 1 <-> Max 왕복. 1 에서 출발하는 cos 곡선이라 램프업 끝과 값이 끊기지 않는다.
		const float PulseTime = IntensityElapsed - IntensityRampUpTime;
		const float PulseAlpha = 0.5f - 0.5f * FMath::Cos(UE_TWO_PI * PulseTime / FMath::Max(IntensityPulsePeriod, 0.01f));
		Intensity = FMath::Lerp(1.0f, IntensityMax, PulseAlpha);
	}

	ApplyRootMeshIntensity(Intensity);
}

void AMagneticGeneratorTower::ApplyRootMeshIntensity(float Intensity)
{
	if (IntensityParameterName.IsNone())
	{
		return;
	}

	const float ClampedIntensity = FMath::Clamp(Intensity, 0.0f, IntensityMax);
	for (UMaterialInstanceDynamic* MID : RootMeshMIDs)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(IntensityParameterName, ClampedIntensity);
		}
	}
}
