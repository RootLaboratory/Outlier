#if WITH_DEV_AUTOMATION_TESTS

#include "Enemy/OutlierSightBoneCache.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyTargetRules.h"
#include "Shooter/ShooterCharacter.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ReferenceSkeleton.h"
#include "Team/OutlierTeamIds.h"

namespace OutlierSightBoneTests
{
	void CheckIndex(FAutomationTestBase& Test, const TCHAR* Message,
		const FOutlierSightBoneCache& Cache, int32 Position, int32 Expected)
	{
		if (Test.TestTrue(Message, Cache.IsValid() && Cache.GetBoneIndices().Num() > Position))
		{
			Test.TestEqual(Message, Cache.GetBoneIndices()[Position], Expected);
		}
	}

	USkeletalMesh* MakeMesh(bool bReverseOrder = false)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
		FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		const FName First = bReverseOrder ? TEXT("body") : TEXT("head");
		const FName Second = bReverseOrder ? TEXT("head") : TEXT("body");
		Modifier.Add(FMeshBoneInfo(First, First.ToString(), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(Second, Second.ToString(), 0), FTransform::Identity);
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSightBoneValidationTest,
	"Outlier.Enemy.SightBones.RegistrationValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSightBoneValidationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = OutlierSightBoneTests::MakeMesh();
	FOutlierSightBoneCache Cache;
	const TArray<FName> Valid = { TEXT("head"), TEXT("body") };
	TestTrue(TEXT("Valid bones resolve"), Cache.Resolve(Mesh, Valid));
	TestEqual(TEXT("All registered bones remain in the denominator"), Cache.GetBoneIndices().Num(), 2);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Head is a reference skeleton index"), Cache, 0, 1);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Body is a reference skeleton index"), Cache, 1, 2);

	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
	Socket->SocketName = TEXT("head_socket");
	Socket->BoneName = TEXT("head");
	Mesh->GetMeshOnlySocketList().Add(Socket);
	Mesh->RebuildSocketMap();
	const TArray<TArray<FName>> InvalidLists = {
		{}, { NAME_None }, { TEXT("head"), TEXT("HEAD") },
		{ TEXT("head"), TEXT("missing") }, { TEXT("head_socket") }
	};
	AddExpectedError(TEXT("Invalid sight bone registration"), EAutomationExpectedErrorFlags::Contains, 6);
	for (const TArray<FName>& Names : InvalidLists)
	{
		TestFalse(TEXT("An invalid list is rejected as a whole"), Cache.Resolve(Mesh, Names));
		TestFalse(TEXT("Rejected cache is invalid"), Cache.IsValid());
		TestTrue(TEXT("Rejected registration has a diagnostic"), !Cache.GetError().IsEmpty());
		TestEqual(TEXT("Rejected registration exposes no partial indices"), Cache.GetBoneIndices().Num(), 0);
		TestFalse(TEXT("Repeated invalid requests remain rejected without another warning"), Cache.Resolve(Mesh, Names));
	}
	TestFalse(TEXT("Missing mesh is rejected"), Cache.Resolve(nullptr, Valid));
	TestTrue(TEXT("A repaired registration is accepted"), Cache.Resolve(Mesh, Valid));
	TestTrue(TEXT("Successful retry clears the diagnostic"), Cache.GetError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSightBoneCacheLifetimeTest,
	"Outlier.Enemy.SightBones.CacheLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSightBoneCacheLifetimeTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* FirstMesh = OutlierSightBoneTests::MakeMesh();
	USkeletalMesh* SecondMesh = OutlierSightBoneTests::MakeMesh(true);
	TArray<FName> Names = { TEXT("head"), TEXT("body") };
	FOutlierSightBoneCache First;
	FOutlierSightBoneCache Second;
	First.Resolve(FirstMesh, Names);
	const uint32 FirstRevision = First.GetRevision();
	const int32* CachedData = First.GetBoneIndices().GetData();
	for (int32 Query = 0; Query < 100; ++Query)
	{
		TestTrue(TEXT("Unchanged registration stays valid"), First.Resolve(FirstMesh, Names));
		TestTrue(TEXT("Repeated queries reuse the index storage"), First.GetBoneIndices().GetData() == CachedData);
		TestEqual(TEXT("Stable registration preserves its observation revision"), First.GetRevision(), FirstRevision);
	}
	TestTrue(TEXT("A different mesh is revalidated"), First.Resolve(SecondMesh, Names));
	TestTrue(TEXT("Mesh replacement advances the observation revision"), First.GetRevision() != FirstRevision);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("New mesh head index replaces old index"), First, 0, 2);
	Second.Resolve(FirstMesh, Names);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Another target keeps its own mesh index"), Second, 0, 1);
	Names = { TEXT("body") };
	First.Resolve(SecondMesh, Names);
	TestEqual(TEXT("A changed list updates the denominator"), First.GetBoneIndices().Num(), 1);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("A changed list resolves the requested bone"), First, 0, 1);
	First.Invalidate();
	TestFalse(TEXT("Explicit invalidation clears validity"), First.IsValid());
	TestEqual(TEXT("Explicit invalidation clears indices"), First.GetBoneIndices().Num(), 0);
	TestTrue(TEXT("Invalidation does not affect another target"), Second.IsValid());
	First.Resolve(SecondMesh, Names);
	SecondMesh->MarkAsGarbage();
	TestFalse(TEXT("A dead mesh immediately invalidates the registration"), First.IsValid());
	AddExpectedError(TEXT("Missing world skeletal mesh"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("An expired mesh cannot reuse a valid cache for null"), First.Resolve(nullptr, Names));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSightBoneTargetsTest,
	"Outlier.Enemy.SightBones.TargetMeshAndPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSightBoneTargetsTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine"), GEngine)) return false;
	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("SightBoneTargets"),
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Target test world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* PartnerClass = LoadClass<APartnerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Partner/BP_PartnerCharacter.BP_PartnerCharacter_C"));
	UClass* EnemyClass = LoadClass<AEnemyBase>(nullptr,
		TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
	if (!TestNotNull(TEXT("Concrete Shooter class"), ShooterClass)
		|| !TestNotNull(TEXT("Concrete Partner class"), PartnerClass)
		|| !TestNotNull(TEXT("Concrete Enemy class"), EnemyClass)) return false;
	// BeginPlay 전의 등록 경로만 검사하므로 이 fixture에는 GAS ActorInfo가 아직 없다.
	AddExpectedError(TEXT("SKIP: ASC 미준비"), EAutomationExpectedErrorFlags::Contains, 2);
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>(PartnerClass, Spawn);
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, Spawn);
	if (!TestNotNull(TEXT("Shooter"), Shooter) || !TestNotNull(TEXT("Partner"), Partner)
		|| !TestNotNull(TEXT("Enemy"), Enemy)) return false;
	USkeletalMesh* FirstMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Workwear/3_Meshes/M_Player1_3.M_Player1_3"));
	USkeletalMesh* SecondMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/1P/Suit/3_Meshes/SKM_Player_01.SKM_Player_01"));
	if (!TestNotNull(TEXT("First world mesh"), FirstMesh) || !TestNotNull(TEXT("Replacement world mesh"), SecondMesh)) return false;
	const int32 FirstHead = FirstMesh->GetRefSkeleton().FindBoneIndex(TEXT("head"));
	const int32 SecondHead = SecondMesh->GetRefSkeleton().FindBoneIndex(TEXT("head"));
	const int32 FirstBody = FirstMesh->GetRefSkeleton().FindBoneIndex(TEXT("spine_03"));
	const int32 SecondBody = SecondMesh->GetRefSkeleton().FindBoneIndex(TEXT("spine_03"));
	if (!TestTrue(TEXT("Test meshes have distinct head indices"), FirstHead != INDEX_NONE && SecondHead != INDEX_NONE && FirstHead != SecondHead)
		|| !TestTrue(TEXT("Test meshes have body bones"), FirstBody != INDEX_NONE && SecondBody != INDEX_NONE)) return false;
	AddInfo(FString::Printf(TEXT("Initial world meshes: Shooter=%s Partner=%s Enemy=%s"),
		*GetPathNameSafe(Shooter->GetMesh()->GetSkeletalMeshAsset()),
		*GetPathNameSafe(Partner->GetMesh()->GetSkeletalMeshAsset()),
		*GetPathNameSafe(Enemy->GetMesh()->GetSkeletalMeshAsset())));
	// 등록만 검사한다. 서로 다른 Skeleton을 교체하는 fixture에서 기존 AnimBP를 평가하지 않는다.
	for (USkeletalMeshComponent* Component : { Shooter->GetMesh(), Partner->GetMesh(), Enemy->GetMesh(),
		Shooter->GetFirstPersonMesh(), Partner->GetFirstPersonMesh() })
	{
		Component->SetAnimInstanceClass(nullptr);
	}
	Shooter->SightBones = { TEXT("head") };
	Partner->SightBones = { TEXT("head") };
	Enemy->SightBones = { TEXT("spine_03") };
	Shooter->GetMesh()->SetSkeletalMeshAsset(FirstMesh);
	Partner->GetMesh()->SetSkeletalMeshAsset(SecondMesh);
	Enemy->GetMesh()->SetSkeletalMeshAsset(SecondMesh);
	// 서로 다른 FP 메시를 넣어도 검사 대상은 월드 메시여야 한다.
	Shooter->GetFirstPersonMesh()->SetSkeletalMeshAsset(SecondMesh);
	Partner->GetFirstPersonMesh()->SetSkeletalMeshAsset(FirstMesh);
	TestTrue(TEXT("Shooter registration is valid"), Shooter->ResolveSightBoneRegistration().IsValid());
	TestTrue(TEXT("Partner registration is valid"), Partner->ResolveSightBoneRegistration().IsValid());
	TestTrue(TEXT("Enemy registration is valid"), Enemy->ResolveSightBoneRegistration().IsValid());
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Shooter uses TP, not FP"), Shooter->ResolveSightBoneRegistration(), 0, FirstHead);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Partner uses world mesh, not FP"), Partner->ResolveSightBoneRegistration(), 0, SecondHead);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Enemy has its own list"), Enemy->ResolveSightBoneRegistration(), 0, SecondBody);
	Shooter->GetMesh()->SetSkeletalMeshAsset(SecondMesh);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Shooter mesh replacement revalidates"), Shooter->ResolveSightBoneRegistration(), 0, SecondHead);
	Partner->GetMesh()->SetSkeletalMeshAsset(FirstMesh);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Partner mesh replacement revalidates"), Partner->ResolveSightBoneRegistration(), 0, FirstHead);
	TestTrue(TEXT("Unpossessed enemy retains Enemy team"), Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Enemy));
	Enemy->SetEnemyPossessed(true);
	TestTrue(TEXT("Possessed enemy retains Player team"), Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Player));
	TestTrue(TEXT("Possessed drone remains a valid existing target category"),
		OutlierEnemyTargetRules::GetPriority(Enemy) == OutlierEnemyTargetRules::ETargetPriority::PossessedDrone);
	Enemy->GetMesh()->SetSkeletalMeshAsset(FirstMesh);
	OutlierSightBoneTests::CheckIndex(*this, TEXT("Possessed mesh replacement revalidates"), Enemy->ResolveSightBoneRegistration(), 0, FirstBody);
	Enemy->SetEnemyPossessed(false);
	TestTrue(TEXT("Possession release restores Enemy team"), Enemy->GetGenericTeamId() == FGenericTeamId(OutlierTeamIds::Enemy));
	TestTrue(TEXT("Bone registration does not force hostile target eligibility"),
		OutlierEnemyTargetRules::GetPriority(Enemy) == OutlierEnemyTargetRules::ETargetPriority::EnemyUnit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSightBoneAssetTest,
	"Outlier.Enemy.SightBones.ProjectSkeletons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSightBoneAssetTest::RunTest(const FString& Parameters)
{
	const TCHAR* MeshPaths[] = {
		TEXT("/Game/Characters/1P/Workwear/3_Meshes/M_Player1_3.M_Player1_3"),
		TEXT("/Game/Characters/1P/Suit/3_Meshes/SKM_Player_01.SKM_Player_01"),
		TEXT("/Game/Characters/2P/Meshes/SKM_Player_02_Body.SKM_Player_02_Body"),
		TEXT("/Game/Characters/Enemy/VECDrone/Meshes/SKM_Drone_Rifle.SKM_Drone_Rifle"),
		TEXT("/Game/Characters/Enemy/VECDrone/Meshes/SKM_Drone_Rifle_GunFwd075.SKM_Drone_Rifle_GunFwd075"),
		TEXT("/Game/Characters/Enemy/VECDrone/Meshes/SKM_SuicideDrone.SKM_SuicideDrone")
	};
	for (const TCHAR* Path : MeshPaths)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
		if (!TestNotNull(Path, Mesh)) continue;
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		if (!TestTrue(TEXT("Project mesh has a reference skeleton"), Skeleton.GetNum() > 0)) continue;
		TArray<FName> Names;
		TArray<FString> DisplayNames;
		for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
		{
			Names.Add(Skeleton.GetBoneName(BoneIndex));
			DisplayNames.Add(Names.Last().ToString());
		}
		FOutlierSightBoneCache Cache;
		TestTrue(TEXT("Real RefSkeleton bone names resolve without socket fallback"), Cache.Resolve(Mesh, Names));
		TestEqual(TEXT("Real mesh registration retains its full denominator"), Cache.GetBoneIndices().Num(), Names.Num());
		// 실측한 이름은 테스트 보고서에만 남긴다. 전체 본 목록을 시야 표본 기본값으로 채택하지 않는다.
		AddInfo(FString::Printf(TEXT("Mesh=%s Bones=%d Names=%s"), Path, Names.Num(), *FString::Join(DisplayNames, TEXT(","))));
	}
	return true;
}

#endif
