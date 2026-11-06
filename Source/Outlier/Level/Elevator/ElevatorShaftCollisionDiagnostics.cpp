#include "Level/Elevator/ElevatorShaftController.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Enemy/SelfDestructDrone.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElevatorCollision, Log, All);

namespace
{
	void AddMovementIgnores(FCollisionQueryParams& Params, const UCapsuleComponent& Capsule)
	{
		// 실제 이동이 무시하는 부착물/컴포넌트를 진단에서 다시 벽으로 판정하지 않는다.
		Params.AddIgnoredActors(Capsule.GetMoveIgnoreActors());
		for (const UPrimitiveComponent* Component : Capsule.GetMoveIgnoreComponents())
		{
			Params.AddIgnoredComponent(Component);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ElevatorCollisionCommand(
		TEXT("outlier.Elevator.CollisionDiagnostics"),
		TEXT("Observe existing elevator collision, drone/Partner capsules and local Partner camera. "
			"Usage: [seconds=10, max=30; 0=stop] [optional controller name]. Run on server and owning client."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) return;
			const float Duration = Args.IsEmpty() ? 10.0f : FCString::Atof(*Args[0]);
			for (TActorIterator<AElevatorShaftController> It(World); It; ++It)
			{
				if (Args.Num() < 2 || It->GetName().Contains(Args[1]))
				{
					It->StartCollisionDiagnostics(Duration);
				}
			}
		}), ECVF_Cheat);

	void LogProbe(const TCHAR* Kind, const AActor* Subject, const FVector& Start,
		const FVector& End, bool bBlocked, const FHitResult& Hit)
	{
		UE_LOG(LogElevatorCollision, Log,
			TEXT("[ElevatorCollision][%s] World=%s Time=%.3f Subject=%s Start=%s End=%s Blocked=%d Actor=%s Component=%s StartPenetrating=%d Depth=%.3f Normal=%s ImpactNormal=%s HitTime=%.4f"),
			Kind, *GetNameSafe(Subject->GetWorld()), Subject->GetWorld()->GetTimeSeconds(),
			*Subject->GetName(), *Start.ToCompactString(), *End.ToCompactString(), bBlocked,
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Hit.bStartPenetrating,
			Hit.PenetrationDepth, *Hit.Normal.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), Hit.Time);
	}
}

void AElevatorShaftController::StartCollisionDiagnostics(float Duration)
{
	StopCollisionDiagnostics();
	if (!FMath::IsFinite(Duration) || Duration <= 0.0f) return;
	Duration = FMath::Clamp(Duration, 0.1f, 30.0f);
	UE_LOG(LogElevatorCollision, Log,
		TEXT("[ElevatorCollision][Begin] World=%s Controller=%s Authority=%d NetMode=%d State=%s Duration=%.1f Location=%s"),
		*GetNameSafe(GetWorld()), *GetName(), HasAuthority(), static_cast<int32>(GetNetMode()),
		*UEnum::GetValueAsString(Motion.State), Duration, *GetActorLocation().ToCompactString());

	// 고정 충돌체는 Controller 자신의 컴포넌트다. Section/Wall의 시각 메시와 문은 여기서 제외한다.
	TInlineComponentArray<UPrimitiveComponent*> Components;
	GetComponents(Components, false);
	for (UPrimitiveComponent* Component : Components)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		const UBodySetup* BodySetup = Component->GetBodySetup();
		const FBodyInstance* Body = Component->GetBodyInstance();
		UE_LOG(LogElevatorCollision, Log,
			TEXT("[ElevatorCollision][Component] Controller=%s Component=%s Mesh=%s Registered=%d ActorCollision=%d Collision=%s ObjectType=%s Profile=%s Pawn=%s Camera=%s PhysicsBody=%s PhysicsState=%d BodyValid=%d Complexity=%s SimpleShapes=%d TriangleMeshes=%d Location=%s Scale=%s BoundsOrigin=%s BoundsExtent=%s"),
			*GetName(), *Component->GetName(), *GetNameSafe(Mesh ? Mesh->GetStaticMesh() : nullptr),
			Component->IsRegistered(), GetActorEnableCollision(), *UEnum::GetValueAsString(Component->GetCollisionEnabled()),
			*UEnum::GetValueAsString(Component->GetCollisionObjectType()), *Component->GetCollisionProfileName().ToString(),
			*UEnum::GetValueAsString(Component->GetCollisionResponseToChannel(ECC_Pawn)),
			*UEnum::GetValueAsString(Component->GetCollisionResponseToChannel(ECC_Camera)),
			*UEnum::GetValueAsString(Component->GetCollisionResponseToChannel(ECC_PhysicsBody)),
			Component->IsPhysicsStateCreated(), Body && Body->IsValidBodyInstance(),
			BodySetup ? *UEnum::GetValueAsString(BodySetup->GetCollisionTraceFlag()) : TEXT("None"),
			BodySetup ? BodySetup->AggGeom.GetElementCount() : 0, BodySetup ? BodySetup->TriMeshGeometries.Num() : 0,
			*Component->GetComponentLocation().ToCompactString(), *Component->GetComponentScale().ToCompactString(),
			*Component->Bounds.Origin.ToCompactString(), *Component->Bounds.BoxExtent.ToCompactString());
	}

	// 관측 시작 시에만 대상을 찾고 이후에는 약한 참조를 사용한다. 재현 도중 새로 생성된 드론은 명령을 다시 실행한다.
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsA<ASelfDestructDrone>() && !It->IsA<APartnerCharacter>()) continue;
		UCapsuleComponent* Capsule = It->GetCapsuleComponent();
		if (!Capsule) continue;
		FCollisionDiagnosticPawn& Sample = CollisionDiagnosticPawns.AddDefaulted_GetRef();
		Sample.Character = *It;
		Sample.PreviousLocation = Capsule->GetComponentLocation();
		Capsule->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnDiagnosticCapsuleHit);
		UE_LOG(LogElevatorCollision, Log,
			TEXT("[ElevatorCollision][Capsule] Pawn=%s Authority=%d Local=%d Role=%d ActorCollision=%d Collision=%s ObjectType=%s WorldStatic=%s WorldDynamic=%s PhysicsBody=%s Radius=%.2f HalfHeight=%.2f"),
			*It->GetName(), It->HasAuthority(), It->IsLocallyControlled(), static_cast<int32>(It->GetLocalRole()),
			It->GetActorEnableCollision(), *UEnum::GetValueAsString(Capsule->GetCollisionEnabled()),
			*UEnum::GetValueAsString(Capsule->GetCollisionObjectType()),
			*UEnum::GetValueAsString(Capsule->GetCollisionResponseToChannel(ECC_WorldStatic)),
			*UEnum::GetValueAsString(Capsule->GetCollisionResponseToChannel(ECC_WorldDynamic)),
			*UEnum::GetValueAsString(Capsule->GetCollisionResponseToChannel(ECC_PhysicsBody)),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	}
	CollisionDiagnosticEndTime = GetWorld()->GetTimeSeconds() + Duration;
	GetWorldTimerManager().SetTimer(CollisionDiagnosticTimer, this, &ThisClass::SampleCollisionDiagnostics, 0.1f, true);
	SampleCollisionDiagnostics();
}

void AElevatorShaftController::StopCollisionDiagnostics()
{
	GetWorldTimerManager().ClearTimer(CollisionDiagnosticTimer);
	for (const FCollisionDiagnosticPawn& Sample : CollisionDiagnosticPawns)
	{
		if (ACharacter* Character = Sample.Character.Get())
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->OnComponentHit.RemoveDynamic(this, &ThisClass::OnDiagnosticCapsuleHit);
			}
		}
	}
	CollisionDiagnosticPawns.Reset();
}

void AElevatorShaftController::OnDiagnosticCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	(void)OtherActor;
	(void)OtherComponent;
	(void)NormalImpulse;
	const double Now = GetWorld()->GetTimeSeconds();
	for (FCollisionDiagnosticPawn& Sample : CollisionDiagnosticPawns)
	{
		if (Sample.Character.Get() != HitComponent->GetOwner()) continue;
		// 비비는 동안 같은 접촉이 매 프레임 발생하므로 대상별로 초당 최대 10회만 기록한다.
		if (Sample.LastHitTime >= 0.0 && Now - Sample.LastHitTime < 0.1) return;
		Sample.LastHitTime = Now;
		LogProbe(TEXT("Contact"), Sample.Character.Get(), Hit.TraceStart, Hit.TraceEnd, Hit.bBlockingHit, Hit);
		return;
	}
}

bool AElevatorShaftController::ProbeDiagnosticCameraPath(const ACharacter& Character,
	const FVector& CameraLocation, bool bUseBodyChannel, FHitResult& OutHit)
{
	OutHit = FHitResult();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (!Character.GetWorld() || !Capsule) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorCameraDiagnostic), false, &Character);
	if (bUseBodyChannel) AddMovementIgnores(Params, *Capsule);
	const FCollisionResponseParams Responses = bUseBodyChannel
		? FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()) : FCollisionResponseParams::DefaultResponseParam;
	return Character.GetWorld()->SweepSingleByChannel(OutHit, Capsule->GetComponentLocation(), CameraLocation,
		FQuat::Identity, bUseBodyChannel ? Capsule->GetCollisionObjectType() : ECC_Camera,
		FCollisionShape::MakeSphere(4.0f), Params, Responses);
}

void AElevatorShaftController::SampleCollisionDiagnostics()
{
	if (GetWorld()->GetTimeSeconds() >= CollisionDiagnosticEndTime)
	{
		UE_LOG(LogElevatorCollision, Log, TEXT("[ElevatorCollision][End] Controller=%s"), *GetName());
		StopCollisionDiagnostics();
		return;
	}
	for (FCollisionDiagnosticPawn& Sample : CollisionDiagnosticPawns)
	{
		ACharacter* Character = Sample.Character.Get();
		if (!Character) continue;
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		if (!Capsule) continue;
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const FVector Location = Capsule->GetComponentLocation();
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorCollisionDiagnostic), false, Character);
		AddMovementIgnores(Params, *Capsule);
		const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
		const bool bOverlapping = GetWorld()->OverlapBlockingTestByChannel(Location, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), Shape, Params, Responses);
		UE_LOG(LogElevatorCollision, Log,
			TEXT("[ElevatorCollision][Sample] World=%s Time=%.3f Controller=%s Pawn=%s Location=%s Delta=%s Velocity=%s MovementMode=%d BlockingOverlap=%d"),
			*GetNameSafe(GetWorld()), GetWorld()->GetTimeSeconds(), *GetName(), *Character->GetName(),
			*Location.ToCompactString(), *(Location - Sample.PreviousLocation).ToCompactString(),
			*Character->GetVelocity().ToCompactString(), Movement ? static_cast<int32>(Movement->MovementMode) : -1, bOverlapping);
		if (!Location.Equals(Sample.PreviousLocation, 0.01f))
		{
			// これは移動処理ではなく通過区間の再検査。Contact と合わせて位置補正・隙間を切り分ける。
			FHitResult Hit;
			const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Sample.PreviousLocation, Location,
				Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Shape, Params, Responses);
			LogProbe(TEXT("MovementReplay"), Character, Sample.PreviousLocation, Location, bBlocked, Hit);
		}
		Sample.PreviousLocation = Location;

		APartnerCharacter* Partner = Cast<APartnerCharacter>(Character);
		APlayerController* Player = Partner ? Cast<APlayerController>(Partner->GetController()) : nullptr;
		// 최종 시점은 소유 클라이언트에서 확정한다. Dedicated의 카메라 위치를 실제 화면으로 해석하지 않는다.
		if (!Player || !Player->IsLocalController() || Player->GetViewTarget() != Partner) continue;
		const UCameraComponent* Camera = Partner->GetFirstPersonCameraComponent();
		if (!Camera || !Player->PlayerCameraManager) continue;
		const FVector CameraLocation = Player->PlayerCameraManager->GetCameraLocation();
		UE_LOG(LogElevatorCollision, Log,
			TEXT("[ElevatorCollision][Camera] Partner=%s Capsule=%s Component=%s FinalPOV=%s Offset=%s ProbeRadius=4"),
			*Partner->GetName(), *Location.ToCompactString(), *Camera->GetComponentLocation().ToCompactString(),
			*CameraLocation.ToCompactString(), *(CameraLocation - Location).ToCompactString());
		// 카메라 전용 채널과 본체의 이동 채널을 나눠 검사한다. 4cm 구는 진단용이며 실제 카메라는 움직이지 않는다.
		FHitResult CameraHit;
		const bool bCameraBlocked = ProbeDiagnosticCameraPath(*Partner, CameraLocation, false, CameraHit);
		LogProbe(TEXT("CameraChannel"), Partner, Location, CameraLocation, bCameraBlocked, CameraHit);
		FHitResult BodyChannelHit;
		const bool bBodyBlocked = ProbeDiagnosticCameraPath(*Partner, CameraLocation, true, BodyChannelHit);
		LogProbe(TEXT("CameraViaBodyChannel"), Partner, Location, CameraLocation, bBodyBlocked, BodyChannelHit);
	}
}
