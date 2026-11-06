#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterAnimInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Weapon/MeleeWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeAimOffsetTest,
	"Outlier.Animation.Shooter.MeleeAimOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeAimOffsetTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UShooterAnimInstance* Anim = NewObject<UShooterAnimInstance>(Mesh);
	// AO 갱신은 무기 설정을 읽기만 한다. 월드/에셋 로드 없이 동일한 런타임 경로를 검사한다.
	AWeaponBase* Weapon = GetMutableDefault<AWeaponBase>();
	Anim->CurrentWeaponType = EWeaponType::Melee;
	for (const ECombatState Phase : { ECombatState::Idle, ECombatState::Attack, ECombatState::Recovery })
	{
		Anim->CombatState = Phase;
		for (const float Pitch : { -60.0f, 0.0f, 60.0f })
		{
			Anim->AimPitch = Pitch;
			Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
			TestEqual(TEXT("Melee retains aim through idle/attack/recovery"), Anim->GetMeleeAimOffsetAlpha(), 1.0f);
			TestEqual(TEXT("Aim pitch remains available to the authored AO"), Anim->AimPitch, Pitch);
			TestEqual(TEXT("Melee does not enable firearm AO"), Anim->ThirdPersonAimOffsetAlpha, 0.0f);
			TestEqual(TEXT("Melee does not enable firearm ADS"), Anim->ThirdPersonADSAlpha, 0.0f);
			TestEqual(TEXT("Melee does not enable firearm upper body"), Anim->ThirdPersonUpperBodyAlpha, 0.0f);
		}
	}
	Anim->bIsSprinting = true;
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Sprint releases melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);
	Anim->bIsSprinting = false;
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Ending sprint restores melee aim"), Anim->GetMeleeAimOffsetAlpha(), 1.0f);
	Anim->bIsDead = true;
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Death releases melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);
	Anim->bIsDead = false;
	Anim->UpdateThirdPersonProceduralState(1.0f, nullptr);
	TestEqual(TEXT("Missing weapon cannot enable melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);
	for (const EWeaponType Type : { EWeaponType::Rifle, EWeaponType::Pistol })
	{
		Anim->CurrentWeaponType = Type;
		Anim->CombatState = ECombatState::Idle;
		Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
		TestEqual(TEXT("Switching to firearms clears melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);
		TestEqual(TEXT("Firearm AO retains its previous eligibility"), Anim->ThirdPersonAimOffsetAlpha, 1.0f);
	}
	Anim->CurrentWeaponType = EWeaponType::Melee;
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	Anim->ResetThirdPersonProceduralState();
	TestEqual(TEXT("Presentation reset clears melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);

	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (!TestNotNull(TEXT("Shooter class loads for equip lifecycle"), ShooterClass)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MeleeAimTest")), GetTransientPackage());
	if (!TestNotNull(TEXT("Equip lifecycle world exists"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass);
	if (!TestNotNull(TEXT("Equip lifecycle owner spawns"), Shooter)) return false;
	Anim->CachedShooterCharacter = Shooter;
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Owned melee enables AO"), Anim->GetMeleeAimOffsetAlpha(), 1.0f);
	Shooter->BeginActionLock(EShooterActionLock::Equip);
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Equip lock releases melee AO"), Anim->GetMeleeAimOffsetAlpha(), 0.0f);
	Shooter->EndActionLock(EShooterActionLock::Equip);
	Anim->UpdateThirdPersonProceduralState(1.0f, Weapon);
	TestEqual(TEXT("Equip completion restores melee AO"), Anim->GetMeleeAimOffsetAlpha(), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierMeleeSocketFreshnessTest,
	"Outlier.Weapon.Melee.SocketFreshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierMeleeSocketFreshnessTest::RunTest(const FString& Parameters)
{
	UClass* HammerClass = LoadClass<AMeleeWeaponBase>(nullptr, TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"));
	if (!TestNotNull(TEXT("Project hammer class loads"), HammerClass)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MeleeSocketTest")), GetTransientPackage());
	if (!TestNotNull(TEXT("Socket test world exists"), World)) return false;
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AMeleeWeaponBase* Weapon = World->SpawnActor<AMeleeWeaponBase>(HammerClass);
	if (!TestNotNull(TEXT("Project hammer spawns"), Weapon)) return false;
	USkeletalMeshComponent* Mesh = Weapon->GetThirdPersonWeaponMesh();
	if (!TestNotNull(TEXT("Hammer TP mesh asset exists"), Mesh->GetSkeletalMeshAsset())) return false;
	// 에셋은 저장하지 않는다. 복제한 손 소켓만 변경해 bone 확정 후 자식 갱신 전의 상태를 재현한다.
	USkeletalMesh* ParentAsset = DuplicateObject<USkeletalMesh>(Mesh->GetSkeletalMeshAsset(), GetTransientPackage());
	USkeletalMeshSocket* Grip = NewObject<USkeletalMeshSocket>(ParentAsset);
	Grip->SocketName = TEXT("TestMeleeGrip");
	Grip->BoneName = ParentAsset->GetRefSkeleton().GetBoneName(0);
	ParentAsset->AddSocket(Grip);
	AActor* ParentActor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Parent = NewObject<USkeletalMeshComponent>(ParentActor);
	ParentActor->SetRootComponent(Parent);
	Parent->SetSkeletalMeshAsset(ParentAsset);
	Parent->RegisterComponent();
	Parent->RefreshBoneTransforms();
	Mesh->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Grip->SocketName);
	for (const float Pitch : { -60.0f, 0.0f, 60.0f })
	{
		const FTransform OldWeapon = Mesh->GetComponentTransform();
		Grip->RelativeLocation += FVector(17.0f, 3.0f, 9.0f);
		Grip->RelativeRotation = FRotator(Pitch, 15.0f, 0.0f);
		const FTransform ExpectedWeapon = Mesh->GetRelativeTransform() * Parent->GetSocketTransform(Grip->SocketName);
		TestFalse(TEXT("Fixture leaves the child attachment stale"), OldWeapon.Equals(ExpectedWeapon));
		FVector Start, End;
		if (!TestTrue(TEXT("Real hammer trace sockets exist"), Weapon->GetMeleeTraceSocketLocations(Start, End))) return false;
		TestTrue(TEXT("Trace refreshes attachment to the latest hand socket"), Mesh->GetComponentTransform().Equals(ExpectedWeapon, 0.001f));
		const FVector ExpectedStart = ExpectedWeapon.TransformPosition(Mesh->GetSocketTransform(Weapon->MeleeTraceStartSocketName, RTS_Component).GetLocation());
		const FVector ExpectedEnd = ExpectedWeapon.TransformPosition(Mesh->GetSocketTransform(Weapon->MeleeTraceEndSocketName, RTS_Component).GetLocation());
		TestTrue(TEXT("Trace start follows current pitch and attachment"), Start.Equals(ExpectedStart, 0.001f));
		TestTrue(TEXT("Trace end follows current pitch and attachment"), End.Equals(ExpectedEnd, 0.001f));
	}
	return true;
}

#endif
