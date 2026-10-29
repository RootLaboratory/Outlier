#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "ReferenceSkeleton.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Weapon/LaserWeapon.h"
#include "Weapon/LaserWeaponFeedbackDefinition.h"
#include "TrailEffectDefinition.h"
#include "Weapon/WeaponCoreRow.h"

namespace
{
	struct FLaserTestWorld
	{
		UWorld* World = nullptr;
		FLaserTestWorld()
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("LaserTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FLaserTestWorld()
		{
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierLaserWeaponConfigurationTest,
	"Outlier.Weapon.Laser.SingleShotConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserWeaponConfigurationTest::RunTest(const FString& Parameters)
{
	FLaserTestWorld Fixture;
	ALaserWeapon* Weapon = Fixture.World->SpawnActor<ALaserWeapon>();
	if (!TestNotNull(TEXT("Laser weapon spawns"), Weapon)) return false;
	UDataTable* Table = NewObject<UDataTable>(Weapon);
	Table->RowStruct = FWeaponCoreRow::StaticStruct();
	FWeaponCoreRow Row;
	Row.FireType = EWeaponFireType::Projectile;
	Row.FireMode = EWeaponFireMode::FullAuto;
	Row.MagazineSize = 1;
	Row.bInfiniteAmmo = false;
	Row.BurstShotCount = 20;
	Row.PostBurstCooldownSeconds = 8.0f;
	Row.DefaultMinBloom = 30.0f;
	Row.DefaultMaxBloom = 40.0f;
	Table->AddRow(TEXT("ConflictingRifle"), Row);
	Weapon->WeaponCoreRow.DataTable = Table;
	Weapon->WeaponCoreRow.RowName = TEXT("ConflictingRifle");
	Weapon->InitializeFromDataTables();
	TestTrue(TEXT("Core row cannot enable projectiles"), Weapon->GetFireType() == EWeaponFireType::HitScan);
	TestTrue(TEXT("Core row cannot enable automatic fire"), Weapon->GetFireMode() == EWeaponFireMode::SemiAuto);
	TestEqual(TEXT("One request is limited to one shot"), Weapon->BurstShotCount, 1);
	TestFalse(TEXT("Automatic timer is disabled"), Weapon->bIsAutomatic != 0);
	TestTrue(TEXT("Infinite ammo is enforced"), Weapon->bInfiniteAmmo != 0);
	const int32 Ammo = Weapon->GetCurrentAmmo();
	for (int32 Index = 0; Index < 5; ++Index) Weapon->ConsumeAmmo();
	TestEqual(TEXT("Repeated shots do not consume ammunition"), Weapon->GetCurrentAmmo(), Ammo);
	TestFalse(TEXT("Reload is unavailable"), Weapon->CanReload());
	TestEqual(TEXT("Conflicting row cannot add spread"), Weapon->GetCurrentSpread(), 0.0f);
	TestEqual(TEXT("Task owns cooldown timing"), Weapon->PostBurstCooldownSeconds, 0.0f);
	TestEqual(TEXT("No reuse cooldown is applied"), Weapon->GetReuseCooldown(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierLaserWeaponPresentationTest,
	"Outlier.Weapon.Laser.ShotAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLaserWeaponPresentationTest::RunTest(const FString& Parameters)
{
	FLaserTestWorld Fixture;
	ACharacter* Owner = Fixture.World->SpawnActor<ACharacter>();
	AAIController* Controller = Fixture.World->SpawnActor<AAIController>();
	ALaserWeapon* Weapon = Fixture.World->SpawnActor<ALaserWeapon>();
	if (!TestNotNull(TEXT("Owner"), Owner) || !TestNotNull(TEXT("Controller"), Controller)
		|| !TestNotNull(TEXT("Weapon"), Weapon)) return false;
	Controller->Possess(Owner);
	Weapon->OnEquipped(Owner);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	// 카메라는 +X를 보고 있어도 실제 소켓이 +Y를 향하면 그 방향으로 발사해야 한다.
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Weapon);
	{
		FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
	}
	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
	Socket->SocketName = TEXT("Muzzle");
	Socket->BoneName = TEXT("root");
	Mesh->GetMeshOnlySocketList().Add(Socket);
	USkeletalMeshSocket* ChargeSocket = NewObject<USkeletalMeshSocket>(Mesh);
	ChargeSocket->SocketName = TEXT("ChargeAnchor");
	ChargeSocket->BoneName = TEXT("root");
	ChargeSocket->RelativeLocation = FVector(20.0f, 0.0f, 0.0f);
	Mesh->GetMeshOnlySocketList().Add(ChargeSocket);
	Mesh->RebuildSocketMap();
	Weapon->ThirdPersonWeaponMesh->SetSkeletalMeshAsset(Mesh);
	Weapon->ThirdPersonWeaponMesh->SetWorldLocationAndRotation(ViewLocation + FVector(0.0f, 50.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));
	FTransform ShotTransform;
	if (!TestTrue(TEXT("Laser resolves an actual socket"), Weapon->ResolveShotTransform(NAME_None, ShotTransform))) return false;
	TestTrue(TEXT("Socket forward overrides camera direction"), ShotTransform.GetUnitAxis(EAxis::X).Equals(FVector::RightVector, 0.001));
	ULaserWeaponFeedbackDefinition* Feedback = NewObject<ULaserWeaponFeedbackDefinition>(Weapon);
	Feedback->ChargeEffect = NewObject<UTrailEffectDefinition>(Feedback);
	Feedback->ChargeEffect->AttachSocketName = TEXT("ChargeAnchor");
	Weapon->FeedbackDefinition = Feedback;
	USkeletalMeshComponent* ResolvedComponent = nullptr;
	FName ResolvedSocket;
	TestTrue(TEXT("Charge and shot share the configured socket"),
		Weapon->ResolveLaserMuzzle(NAME_None, ResolvedComponent, ResolvedSocket, ShotTransform)
		&& ResolvedSocket == TEXT("ChargeAnchor"));
	Feedback->ChargeEffect->AttachSocketName = TEXT("MissingSocket");
	TestFalse(TEXT("Missing socket cannot fall back to camera fire"), Weapon->ResolveShotTransform(NAME_None, ShotTransform));
	Feedback->ChargeEffect->AttachSocketName = TEXT("ChargeAnchor");
	Weapon->ResolveShotTransform(NAME_None, ShotTransform);
	TestFalse(TEXT("Laser uses only its Charge/Trail/Impact presentation"), Weapon->ShouldPlayDefaultFireFX());
	const FVector ShotStart = ShotTransform.GetLocation();
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10.0f, 100.0f, 100.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Box->SetBoxExtent(FVector(100.0f, 10.0f, 100.0f));
	Wall->SetActorLocation(ShotStart + FVector(0.0f, 300.0f, 0.0f));
	Weapon->BeginCharge(1.0f, 0.25f);
	const FLaserWeaponPresentation Telegraph = Weapon->LaserPresentation;
	TestTrue(TEXT("Telegraph alone does not start an attack"), !Weapon->IsAttacking());
	Weapon->StartAttack();
	TestTrue(TEXT("Actual shot enters firing presentation"),
		Weapon->LaserPresentation.Phase == ELaserWeaponPresentationPhase::Firing);
	TestEqual(TEXT("Exactly one shot advances the presentation once"),
		Weapon->LaserPresentation.Revision, Telegraph.Revision + 1);
	TestFalse(TEXT("Single shot finishes attacking immediately"), Weapon->IsAttacking());
	TestFalse(TEXT("No automatic fire timer remains"),
		Fixture.World->GetTimerManager().IsTimerActive(Weapon->AutoFireTimerHandle));
	TestTrue(TEXT("Actual collision endpoint stops at the wall rather than aim target"),
		Weapon->LaserPresentation.bHasImpact
		&& FMath::IsNearlyEqual(Weapon->LaserPresentation.EndPoint.Y, ShotStart.Y + 290.0, 1.0)
		&& FMath::IsNearlyEqual(Weapon->LaserPresentation.EndPoint.X, ShotStart.X, 1.0));
	TestTrue(TEXT("Beam starts at the same server socket as damage trace"),
		Weapon->LaserPresentation.ShotStart.Equals(ShotStart, 1.0));
	TestTrue(TEXT("Internal single-shot StopAttack preserves beam presentation"),
		Weapon->LocalPresentation.Phase == ELaserWeaponPresentationPhase::Firing);
	const uint32 FiredRevision = Weapon->LaserPresentation.Revision;
	Fixture.World->Tick(LEVELTICK_All, 0.05f);
	TestEqual(TEXT("World tick does not emit a second shot"), Weapon->LaserPresentation.Revision, FiredRevision);
	Weapon->StopAttack();
	TestTrue(TEXT("External cancellation clears server presentation"),
		Weapon->LaserPresentation.Phase == ELaserWeaponPresentationPhase::Idle);
	const uint32 CancelRevision = Weapon->LocalPresentation.Revision;
	Weapon->ApplyPresentation(Telegraph);
	TestEqual(TEXT("Late telegraph snapshot cannot resurrect cancelled presentation"),
		Weapon->LocalPresentation.Revision, CancelRevision);
	Weapon->Tick(0.01f);
	TestFalse(TEXT("Idle laser does not keep ticking"), Weapon->IsActorTickEnabled());
	Weapon->BeginCharge(1.0f, 0.25f);
	Weapon->OnUnequipped();
	TestTrue(TEXT("Unequip clears presentation"),
		Weapon->LaserPresentation.Phase == ELaserWeaponPresentationPhase::Idle);
	Weapon->OnEquipped(Owner);
	Weapon->BeginCharge(1.0f, 0.25f);
	Owner->SetActorHiddenInGame(true);
	Weapon->Tick(0.01f);
	TestTrue(TEXT("Hidden owner cancels telegraph"),
		Weapon->LaserPresentation.Phase == ELaserWeaponPresentationPhase::Idle);
	return true;
}

#endif
