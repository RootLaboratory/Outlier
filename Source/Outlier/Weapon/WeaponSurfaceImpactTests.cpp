#if WITH_DEV_AUTOMATION_TESTS

#include "Weapon/WeaponBase.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSurfaceImpactTest,
	"Outlier.Weapon.SurfaceImpact.MaterialQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSurfaceImpactTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("Engine is initialized"), GEngine)) return false;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Read-only cube fixture"), Cube)) return false;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Surface query test world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
	};
	auto SyncPhysicsMaterials = [&]()
	{
		// 새 재질의 물리 명령을 처리한 뒤 조회용 재질 목록까지 동기화한다.
		// Flush만으로는 EndFrame에서 갱신하는 QueryMaterials가 준비되지 않는다.
		World->GetPhysicsScene()->Flush();
		World->GetPhysicsScene()->EndFrame();
	};

	UPhysicalMaterial* OriginalMaterial = NewObject<UPhysicalMaterial>();
	OriginalMaterial->SurfaceType = SurfaceType1;
	UPhysicalMaterial* MetalMaterial = NewObject<UPhysicalMaterial>();
	MetalMaterial->SurfaceType = SurfaceType2;
	UPhysicalMaterial* GlassMaterial = NewObject<UPhysicalMaterial>();
	GlassMaterial->SurfaceType = SurfaceType3;
	UPhysicalMaterial* DefaultMaterial = NewObject<UPhysicalMaterial>();
	DefaultMaterial->SurfaceType = SurfaceType_Default;

	auto AddCube = [&](const FVector& Location, UPhysicalMaterial* Material) -> UStaticMeshComponent*
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor) return nullptr;
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
		Actor->SetRootComponent(Component);
		Actor->AddInstanceComponent(Component);
		Component->SetStaticMesh(Cube);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
		Component->SetPhysMaterialOverride(Material);
		Component->SetWorldLocation(Location);
		Component->RegisterComponent();
		return Component;
	};
	// 공유 Engine 에셋은 읽기만 하고 재질·위치는 테스트 컴포넌트에만 설정한다.
	UStaticMeshComponent* Target = AddCube(FVector::ZeroVector, MetalMaterial);
	UStaticMeshComponent* Other = AddCube(FVector(0.0, 0.0, 2.0), GlassMaterial);
	if (!TestNotNull(TEXT("Target cube"), Target) || !TestNotNull(TEXT("Overlapping cube"), Other)) return false;
	if (!TestNotNull(TEXT("Surface query physics scene"), World->GetPhysicsScene())) return false;
	SyncPhysicsMaterials();

	FHitResult Hit(Target->GetOwner(), Target, FVector(0.0, 0.0, 50.0), FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.PhysMaterial = OriginalMaterial;
	Hit.Distance = 123.0f;
	Hit.Time = 0.25f;
	Hit.BoneName = TEXT("OriginalBone");
	const FHitResult OriginalHit = Hit;
	const uint8 OriginalSurface = static_cast<uint8>(SurfaceType1);
	auto Resolve = [](const FHitResult& Input) { return AWeaponBase::ResolveSurfaceImpact(Input); };
	auto ExpectOriginal = [&](const TCHAR* Label, const FHitResult& Input)
	{
		TestEqual(Label, Resolve(Input), OriginalSurface);
	};

	// UE 컴포넌트 조회는 교차 성공과 월드 Trace의 Blocking 플래그가 일치하지 않는다.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponSurfaceMaterialTest), true);
	QueryParams.bReturnPhysicalMaterial = true;
	FHitResult ComponentHit;
	if (!TestTrue(TEXT("Component query finds the complex surface"), Target->LineTraceComponent(
		ComponentHit, Hit.ImpactPoint + FVector::UpVector * 5.0, Hit.ImpactPoint - FVector::UpVector * 5.0, QueryParams))) return false;
	TestFalse(TEXT("UE component query reports Touch despite finding the surface"), ComponentHit.bBlockingHit);
	TestTrue(TEXT("Component query returns the original target component"), ComponentHit.GetComponent() == Target);
	TestNotNull(TEXT("Component query returns a synchronized physical material"), ComponentHit.PhysMaterial.Get());
	TestTrue(TEXT("Component query returns the Metal fixture material"), ComponentHit.PhysMaterial.Get() == MetalMaterial);
	AddInfo(FString::Printf(TEXT("[SurfaceQueryFixture] Component='%s' ExpectedComponent='%s' PM='%s' ExpectedPM='%s' Face=%d"),
		*GetNameSafe(ComponentHit.GetComponent()), *GetNameSafe(Target), *GetNameSafe(ComponentHit.PhysMaterial.Get()),
		*GetNameSafe(MetalMaterial), ComponentHit.FaceIndex));
	TestEqual(TEXT("Complex material replaces the original surface"), Resolve(Hit), static_cast<uint8>(SurfaceType2));
	bool bResolvedMaterial = false;
	AWeaponBase::ResolveSurfaceImpact(Hit, &bResolvedMaterial);
	TestTrue(TEXT("Successful material query is reported independently of surface value"), bResolvedMaterial);
	TestTrue(TEXT("Original actor is preserved"), Hit.GetActor() == OriginalHit.GetActor());
	TestTrue(TEXT("Original component is preserved"), Hit.GetComponent() == OriginalHit.GetComponent());
	TestTrue(TEXT("Original physical material is preserved"), Hit.PhysMaterial == OriginalHit.PhysMaterial);
	TestTrue(TEXT("Original impact point and normal are preserved"),
		Hit.ImpactPoint == OriginalHit.ImpactPoint && Hit.ImpactNormal == OriginalHit.ImpactNormal);
	TestEqual(TEXT("Original distance is preserved"), Hit.Distance, OriginalHit.Distance);
	TestEqual(TEXT("Original time is preserved"), Hit.Time, OriginalHit.Time);
	TestEqual(TEXT("Original bone is preserved"), Hit.BoneName, OriginalHit.BoneName);

	// 더 가까운 다른 컴포넌트가 있어도 원래 명중한 컴포넌트의 결과만 사용한다.
	Target->SetWorldLocation(FVector(300.0, 0.0, 0.0));
	ExpectOriginal(TEXT("Nearby component is never substituted when target misses"), Hit);
	AWeaponBase::ResolveSurfaceImpact(Hit, &bResolvedMaterial);
	TestFalse(TEXT("Fallback resets the material query success flag"), bResolvedMaterial);
	Target->SetWorldLocation(FVector::ZeroVector);

	Target->SetPhysMaterialOverride(DefaultMaterial);
	SyncPhysicsMaterials();
	TestEqual(TEXT("Valid Default material is accepted instead of original Marble"),
		Resolve(Hit), static_cast<uint8>(SurfaceType_Default));
	AWeaponBase::ResolveSurfaceImpact(Hit, &bResolvedMaterial);
	TestTrue(TEXT("Default physical material still reports query success"), bResolvedMaterial);
	Target->SetPhysMaterialOverride(MetalMaterial);
	SyncPhysicsMaterials();

	FHitResult Invalid = Hit;
	Invalid.bBlockingHit = false;
	ExpectOriginal(TEXT("Non-blocking hit falls back"), Invalid);
	Invalid = Hit;
	Invalid.bStartPenetrating = true;
	ExpectOriginal(TEXT("Initial penetration falls back"), Invalid);
	Invalid = Hit;
	Invalid.ImpactNormal = FVector::ZeroVector;
	ExpectOriginal(TEXT("Invalid normal falls back"), Invalid);
	Invalid = Hit;
	Invalid.ImpactNormal *= 10.0;
	TestEqual(TEXT("Normal is normalized before computing the short trace"), Resolve(Invalid), static_cast<uint8>(SurfaceType2));
	Invalid = Hit;
	Invalid.ImpactPoint.Z += 20.0;
	ExpectOriginal(TEXT("Surface beyond the ten-centimeter segment falls back"), Invalid);
	Invalid = Hit;
	Invalid.Component = nullptr;
	ExpectOriginal(TEXT("Missing component falls back"), Invalid);
	Invalid = Hit;
	Invalid.Component = NewObject<USkeletalMeshComponent>(Target->GetOwner());
	ExpectOriginal(TEXT("Skeletal mesh retains original surface"), Invalid);
	Invalid.Component = NewObject<UBoxComponent>(Target->GetOwner());
	ExpectOriginal(TEXT("Dedicated hitbox retains original surface"), Invalid);
	Invalid.Component = NewObject<UStaticMeshComponent>(Target->GetOwner());
	ExpectOriginal(TEXT("Static mesh without collision falls back"), Invalid);
	Invalid.PhysMaterial = nullptr;
	TestEqual(TEXT("Missing original material uses Default after query failure"),
		Resolve(Invalid), static_cast<uint8>(SurfaceType_Default));
	return true;
}

#endif
