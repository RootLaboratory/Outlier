#if WITH_DEV_AUTOMATION_TESTS

#include "Weapon/WeaponBase.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Enemy/EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TAutoConsoleVariable<FString> CVarSurfaceLoadMesh(TEXT("Outlier.Weapon.SurfaceLoadMesh"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("Static Mesh object path for the 50-drone surface query benchmark."));
	TAutoConsoleVariable<float> CVarSurfaceLoadBudget(TEXT("Outlier.Weapon.SurfaceLoadBudgetMs"), 0.0f,
		TEXT("Optional P95 budget for a batch of 50 simple traces plus complex material queries. 0 reports without a timing limit."));

	constexpr int32 DroneCount = 50;
	constexpr int32 WarmupBatches = 30;
	constexpr int32 MeasuredBatches = 300;

	struct FSurfaceLoadSample
	{
		double BaselineMs = 0.0;
		double ComplexMs = 0.0;
		int32 BaselineHits = 0;
		int32 ComplexHits = 0;
		int32 Resolved = 0;
	};

	struct FSurfaceLoadRun : IAutomationLatentCommand
	{
		FAutomationTestBase& Test;
		TFunction<uint8(const FHitResult&, bool*)> Resolve;
		TStrongObjectPtr<UWorld> World;
		TStrongObjectPtr<UPhysicalMaterial> OriginalMaterial;
		TArray<AEnemyBase*> Drones;
		TArray<UStaticMeshComponent*> Targets;
		TArray<FSurfaceLoadSample> Samples;
		FCollisionQueryParams QueryParams;
		FString MeshPath;
		double Started = FPlatformTime::Seconds();
		int32 Batch = 0;
		IConsoleVariable* DebugTags = nullptr;
		int32 PreviousDebug = 0;
		EConsoleVariableFlags PreviousPriority = ECVF_SetByConstructor;
		bool bInitialized = false;
		bool bArena = false;
		TArray<TPair<FVector, FVector>> ArenaRays;

		FSurfaceLoadRun(FAutomationTestBase& InTest, TFunction<uint8(const FHitResult&, bool*)> InResolve, bool bInArena = false)
			: Test(InTest), Resolve(MoveTemp(InResolve)), QueryParams(SCENE_QUERY_STAT(WeaponSurfaceLoad), false), bArena(bInArena)
		{
			QueryParams.bReturnPhysicalMaterial = true;
		}

		~FSurfaceLoadRun()
		{
			// 실패·중단 때도 임시 월드와 측정을 위해 끈 진단 설정을 복구한다.
			if (World.IsValid() && !bArena)
			{
				GEngine->ShutdownWorldNetDriver(World.Get());
				World->DestroyWorld(true);
				World->SetPhysicsScene(nullptr);
				GEngine->DestroyWorldContext(World.Get());
			}
			if (DebugTags) DebugTags->Set(PreviousDebug, PreviousPriority);
		}

		bool InitializeArena()
		{
			// 에디터에서 열린 맵을 빌린다. 맵 전환·저장이나 게임플레이 실행은 하지 않는다.
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					Test.AddError(TEXT("Stop PIE before running the Arena editor-map trace benchmark."));
					return false;
				}
				if (Context.WorldType == EWorldType::Editor) World.Reset(Context.World());
			}
			if (!World.IsValid() || !World->GetOutermost()->GetName().StartsWith(TEXT("/Game/Maps/Arena/")))
			{
				Test.AddError(TEXT("Open /Game/Maps/Arena/L_Arena_Persistent and make the desired sublevels visible before running this test."));
				return false;
			}
			MeshPath = World->GetOutermost()->GetName();
			TArray<UStaticMeshComponent*> Candidates;
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
			{
				TInlineComponentArray<UStaticMeshComponent*> Components(*It);
				for (UStaticMeshComponent* Component : Components)
				{
					if (Component->IsRegistered() && Component->IsQueryCollisionEnabled()
						&& Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
						&& Component->GetStaticMesh() && It->GetLevel()->bIsVisible)
						Candidates.Add(Component);
				}
			}
			Candidates.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
				{ return A.GetPathName() < B.GetPathName(); });
			// 이름순 앞부분에 있는 같은 메시만 선택되지 않도록 고정 시드로 섞는다.
			constexpr int32 SelectionSeed = 50300;
			FRandomStream Random(SelectionSeed);
			for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
				Candidates.Swap(Index, Random.RandRange(0, Index));
			int32 TriangleQueryTargets = 0;
			const FVector Axes[] = { FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
				-FVector::ForwardVector, -FVector::RightVector, -FVector::UpVector };
			for (UStaticMeshComponent* Candidate : Candidates)
			{
				// 실제 월드 Trace로 해당 메시가 맞는 경로만 채택한다. 탐색 비용은 측정에서 제외한다.
				const FBoxSphereBounds Bounds = Candidate->Bounds;
				for (const FVector& Axis : Axes)
				{
					const FVector Start = Bounds.Origin + Axis * (Bounds.BoxExtent.GetAbsMax() + 100.0);
					const FVector End = Bounds.Origin - Axis * (Bounds.BoxExtent.GetAbsMax() + 100.0);
					FHitResult Hit;
					if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams)
						|| Hit.GetComponent() != Candidate || Hit.bStartPenetrating) continue;
					ArenaRays.Emplace(Start, End);
					Targets.Add(Candidate);
					const UBodySetup* BodySetup = Candidate->GetStaticMesh()->GetBodySetup();
					const int32 CollisionMode = BodySetup ? int32(BodySetup->GetCollisionTraceFlag()) : -1;
					bool bResolved = false;
					const uint8 Surface = Resolve(Hit, &bResolved);
					TriangleQueryTargets += bResolved && CollisionMode == int32(CTF_UseSimpleAndComplex);
					Test.AddInfo(FString::Printf(TEXT("[SurfaceLoad50] Target=%d Component='%s' Mesh='%s' Level='%s' CollisionMode=%d OriginalPM='%s' Surface=%d Resolved=%d"),
						Targets.Num(), *Candidate->GetPathName(), *Candidate->GetStaticMesh()->GetPathName(),
						*Candidate->GetOwner()->GetLevel()->GetOutermost()->GetName(),
						CollisionMode, *GetNameSafe(Hit.PhysMaterial.Get()), Surface, bResolved));
					break;
				}
				if (ArenaRays.Num() == DroneCount) break;
			}
			if (!Test.TestEqual(TEXT("Arena provides fifty distinct static-mesh targets"), ArenaRays.Num(), DroneCount)) return false;
			if (!Test.TestTrue(TEXT("Arena includes successful Simple And Complex targets, not only substituted simple geometry"), TriangleQueryTargets > 0)) return false;
			Samples.Reserve(MeasuredBatches);
			Test.AddInfo(FString::Printf(TEXT("[SurfaceLoad50] Begin Map='%s' Candidates=%d Targets=50 TriangleQueryTargets=%d Seed=%d Warmup=%d Samples=%d Scope=TraceOnly SyntheticDrones=0 AI=Excluded WorldTick=Excluded AudioVFX=Excluded"),
				*MeshPath, Candidates.Num(), TriangleQueryTargets, SelectionSeed, WarmupBatches, MeasuredBatches));
			return true;
		}

		bool Initialize()
		{
			if (!Test.TestNotNull(TEXT("Engine is initialized"), GEngine)) return false;
			// 두 모드 모두 태그 로그 비용을 제외하고 종료 시 원래 설정으로 복원한다.
			DebugTags = IConsoleManager::Get().FindConsoleVariable(TEXT("Outlier.Audio.DebugTags"));
			if (DebugTags)
			{
				PreviousDebug = DebugTags->GetInt();
				PreviousPriority = static_cast<EConsoleVariableFlags>(DebugTags->GetFlags() & ECVF_SetByMask);
				DebugTags->Set(0, PreviousPriority);
			}
			if (bArena) return InitializeArena();
			MeshPath = CVarSurfaceLoadMesh.GetValueOnGameThread();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			UClass* DroneClass = LoadClass<AEnemyBase>(nullptr,
				TEXT("/Game/Blueprints/Enemy/VECDrone/BP_VECDrone_Gun.BP_VECDrone_Gun_C"));
			if (!Test.TestNotNull(TEXT("Benchmark target mesh loads"), Mesh)
				|| !Test.TestNotNull(TEXT("Gun drone BP loads"), DroneClass)) return false;
			const UBodySetup* BodySetup = Mesh->GetBodySetup();
			// Simple을 Complex로 대체한 에셋을 사용해 삼각형 조회가 빠르다고 오판하지 않는다.
			if (!Test.TestTrue(TEXT("Target requires Simple And Complex collision for the comparison"),
				BodySetup && BodySetup->GetCollisionTraceFlag() == CTF_UseSimpleAndComplex)) return false;
			World.Reset(UWorld::CreateWorld(EWorldType::Game, false));
			if (!Test.TestNotNull(TEXT("Benchmark world exists"), World.Get())) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
			OriginalMaterial.Reset(NewObject<UPhysicalMaterial>());
			OriginalMaterial->SurfaceType = SurfaceType1;
			UPhysicalMaterial* Metal = NewObject<UPhysicalMaterial>(World.Get());
			Metal->SurfaceType = SurfaceType2;
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
				UMaterial::GetDefaultMaterial(MD_Surface), World.Get());
			if (!Test.TestNotNull(TEXT("Benchmark material exists"), Material)) return false;
			Material->PhysMaterial = Metal;

			Drones.Reserve(DroneCount);
			Targets.Reserve(DroneCount);
			Samples.Reserve(MeasuredBatches);
			for (int32 Index = 0; Index < DroneCount; ++Index)
			{
				const FVector Location((Index % 10) * 500.0, (Index / 10) * 1500.0, 200.0);
				const FTransform Transform(Location);
				AEnemyBase* Drone = World->SpawnActorDeferred<AEnemyBase>(DroneClass, Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Test.TestNotNull(TEXT("Benchmark drone spawns"), Drone)) return false;
				Drone->AutoPossessAI = EAutoPossessAI::Disabled;
				Drone->FinishSpawning(Transform);
				Drone->SetActorTickEnabled(false);
				Drones.Add(Drone);
				QueryParams.AddIgnoredActor(Drone);

				AActor* TargetActor = World->SpawnActor<AActor>();
				if (!Test.TestNotNull(TEXT("Benchmark target actor spawns"), TargetActor)) return false;
				UStaticMeshComponent* Target = NewObject<UStaticMeshComponent>(TargetActor);
				TargetActor->SetRootComponent(Target);
				TargetActor->AddInstanceComponent(Target);
				Target->SetStaticMesh(Mesh);
				Target->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Target->SetCollisionResponseToAllChannels(ECR_Block);
				// 공유 에셋을 수정하지 않고 컴포넌트의 모든 슬롯에 테스트 재질만 연결한다.
				for (int32 Slot = 0; Slot < Target->GetNumMaterials(); ++Slot) Target->SetMaterial(Slot, Material);
				Target->SetWorldLocation(Drone->GetPawnViewLocation() + FVector(0.0, 500.0, 0.0));
				Target->RegisterComponent();
				Targets.Add(Target);
			}
			if (!Test.TestNotNull(TEXT("Benchmark physics scene exists"), World->GetPhysicsScene())) return false;
			// 물리 프레임이 없는 임시 월드이므로 새 재질과 충돌 형상을 측정 전에 준비한다.
			// 이 비용은 워밍업·측정에 포함하지 않고, 실제 측정 중에는 동기화하지 않는다.
			World->GetPhysicsScene()->Flush();
			World->GetPhysicsScene()->EndFrame();
			// AI·이동·무기 재생을 시작하지 않는다. 측정 범위는 실제 드론 위치의 Trace 작업만이다.
			Test.AddInfo(FString::Printf(TEXT("[SurfaceLoad50] Begin Drones=%d Mesh='%s' Warmup=%d Samples=%d Scope=TraceOnly AI=Excluded WorldTick=Excluded AudioVFX=Excluded"),
				Drones.Num(), *MeshPath, WarmupBatches, MeasuredBatches));
			return Test.TestEqual(TEXT("Exactly fifty real BP drones exist"), Drones.Num(), DroneCount);
		}

		double Measure(bool bComplex, int32& Hits, int32& Resolved)
		{
			const double Start = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < DroneCount; ++Index)
			{
				FHitResult Hit;
				const FVector StartPoint = bArena ? ArenaRays[Index].Key : Drones[Index]->GetPawnViewLocation();
				const FVector EndPoint = bArena ? ArenaRays[Index].Value : Targets[Index]->GetComponentLocation();
				if (!World->LineTraceSingleByChannel(Hit, StartPoint, EndPoint, ECC_Visibility, QueryParams)
					|| Hit.GetComponent() != Targets[Index]) continue;
				++Hits;
				// 양쪽 측정에 같은 입력을 사용한다. Marble을 원래 재질로 두어 Metal 반환이
				// 실제 Complex 조회 성공인지 확인하고, fallback만 반복한 저비용 결과를 걸러낸다.
				if (!bArena) Hit.PhysMaterial = OriginalMaterial.Get();
				bool bResolved = false;
				const uint8 Surface = bComplex ? Resolve(Hit, &bResolved)
					: static_cast<uint8>(UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get()));
				Resolved += bArena ? bResolved : (bResolved && Surface == static_cast<uint8>(SurfaceType2));
			}
			return (FPlatformTime::Seconds() - Start) * 1000.0;
		}

		void Report()
		{
			TArray<double> Baseline, Complex, Delta;
			FString CSV = TEXT("Sample,ComplexFirst,Queries,BaselineMs,WithComplexMs,DeltaMs,BaselineHits,ComplexHits,Resolved,Fallback,MeshOrMap\n");
			double BaselineTotal = 0.0, ComplexTotal = 0.0;
			int32 Hits = 0, Resolved = 0;
			for (int32 Index = 0; Index < Samples.Num(); ++Index)
			{
				const FSurfaceLoadSample& Sample = Samples[Index];
				Baseline.Add(Sample.BaselineMs);
				Complex.Add(Sample.ComplexMs);
				Delta.Add(Sample.ComplexMs - Sample.BaselineMs);
				BaselineTotal += Sample.BaselineMs;
				ComplexTotal += Sample.ComplexMs;
				Hits += Sample.ComplexHits;
				Resolved += Sample.Resolved;
				CSV += FString::Printf(TEXT("%d,%d,%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,\"%s\"\n"),
					Index, (Index + WarmupBatches) % 2, DroneCount, Sample.BaselineMs, Sample.ComplexMs,
					Sample.ComplexMs - Sample.BaselineMs, Sample.BaselineHits, Sample.ComplexHits,
					Sample.Resolved, Sample.ComplexHits - Sample.Resolved, *MeshPath);
			}
			Baseline.Sort(); Complex.Sort(); Delta.Sort();
			const int32 P95 = FMath::CeilToInt(Samples.Num() * 0.95) - 1;
			const double MeanDelta = (ComplexTotal - BaselineTotal) / Samples.Num();
			Test.AddInfo(FString::Printf(TEXT("[SurfaceLoad50] Result Samples=%d Hits=%d Resolved=%d Fallback=%d BaselineAvgMs=%.6f BaselineP95Ms=%.6f BaselineMaxMs=%.6f WithComplexAvgMs=%.6f WithComplexP95Ms=%.6f WithComplexMaxMs=%.6f DeltaAvgMs=%.6f DeltaP95Ms=%.6f DeltaMaxMs=%.6f DeltaUsPerHit=%.3f"),
				Samples.Num(), Hits, Resolved, Hits - Resolved, BaselineTotal / Samples.Num(), Baseline[P95], Baseline.Last(),
				ComplexTotal / Samples.Num(), Complex[P95], Complex.Last(), MeanDelta, Delta[P95], Delta.Last(), MeanDelta * 1000.0 / DroneCount));
			const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(),
				bArena ? TEXT("Reports/WeaponSurfaceLoad50Arena.csv") : TEXT("Reports/WeaponSurfaceLoad50.csv"));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
			if (Test.TestTrue(TEXT("CSV report is saved"), FFileHelper::SaveStringToFile(CSV, *ReportPath)))
				Test.AddInfo(FString::Printf(TEXT("[SurfaceLoad50] CSV='%s' TimingLimitMs=%.3f"), *ReportPath, CVarSurfaceLoadBudget.GetValueOnGameThread()));
			Test.TestEqual(TEXT("Every requested query was measured"), Hits, MeasuredBatches * DroneCount);
			if (!bArena) Test.TestEqual(TEXT("Every complex query resolved Metal instead of fallback"), Resolved, Hits);
			else Test.TestTrue(TEXT("Arena includes successful complex material queries"), Resolved > 0);
			const float Budget = CVarSurfaceLoadBudget.GetValueOnGameThread();
			if (Budget > 0.0f) Test.TestTrue(TEXT("P95 batch time is within the configured budget"), Complex[P95] <= Budget);
		}

		virtual bool Update() override
		{
			if (!bInitialized)
			{
				bInitialized = Initialize();
				if (!bInitialized) return true;
			}
			if (FPlatformTime::Seconds() - Started > 180.0)
			{
				Test.AddError(TEXT("SurfaceLoad50 exceeded 180 seconds. Measurement aborted."));
				return true;
			}
			if (bArena)
			{
				bool bStillOpen = false;
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE)
					{
						Test.AddError(TEXT("PIE started during the Arena benchmark. Measurement aborted."));
						return true;
					}
					bStillOpen |= Context.WorldType == EWorldType::Editor && Context.World() == World.Get();
				}
				if (!bStillOpen)
				{
					Test.AddError(TEXT("Arena editor world changed during measurement. Measurement aborted."));
					return true;
				}
			}
			FSurfaceLoadSample Sample;
			int32 Ignored = 0;
			// 선행 측정의 캐시 이득이 한쪽에만 쌓이지 않도록 순서를 번갈아 실행한다.
			if (Batch % 2 == 0)
			{
				Sample.BaselineMs = Measure(false, Sample.BaselineHits, Ignored);
				Sample.ComplexMs = Measure(true, Sample.ComplexHits, Sample.Resolved);
			}
			else
			{
				Sample.ComplexMs = Measure(true, Sample.ComplexHits, Sample.Resolved);
				Sample.BaselineMs = Measure(false, Sample.BaselineHits, Ignored);
			}
			if (Sample.BaselineHits != DroneCount || Sample.ComplexHits != DroneCount || (!bArena && Sample.Resolved != DroneCount))
			{
				Test.AddError(FString::Printf(TEXT("[SurfaceLoad50] Invalid batch=%d BaselineHits=%d ComplexHits=%d Resolved=%d Expected=50. Check mesh collision geometry and the 10cm material-query segment; this is not a passing performance result."),
					Batch, Sample.BaselineHits, Sample.ComplexHits, Sample.Resolved));
				return true;
			}
			if (Batch >= WarmupBatches) Samples.Add(Sample);
			if (++Batch < WarmupBatches + MeasuredBatches) return false;
			Report();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSurfaceLoadTest,
	"Outlier.Weapon.SurfaceImpact.Load50Drones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FWeaponSurfaceLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSurfaceLoadRun>(*this,
		[](const FHitResult& Hit, bool* bResolved) { return AWeaponBase::ResolveSurfaceImpact(Hit, bResolved); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSurfaceArenaLoadTest,
	"Outlier.Weapon.SurfaceImpact.Load50DronesArena",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FWeaponSurfaceArenaLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSurfaceLoadRun>(*this,
		[](const FHitResult& Hit, bool* bResolved) { return AWeaponBase::ResolveSurfaceImpact(Hit, bResolved); }, true));
	return true;
}

#endif
