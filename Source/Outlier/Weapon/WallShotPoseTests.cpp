#if WITH_DEV_AUTOMATION_TESTS

#include "Weapon/RangedWeaponBase.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "Shooter/Anim/WeaponWallShotPose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimNodes/AnimNode_ApplyAdditive.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#if WITH_EDITOR
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#endif
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
// 실제 연결된 그래프의 노드와 PoseLink를 함께 남긴다. 노드 이름만으로
// 회피 애니메이션과 Transform Bone의 적용 순서를 추측하지 않는다.
void ExportGraph(const TCHAR* ClassPath, TArray<FString>& Lines)
{
	UClass* Class = LoadClass<UAnimInstance>(nullptr, ClassPath);
	if (!Class) return;
	Lines.Add(Class->GetPathName());
	UObject* Default = Class->GetDefaultObject();
	for (TFieldIterator<FStructProperty> It(Class); It; ++It)
	{
		if (!It->Struct->IsChildOf(FAnimNode_Base::StaticStruct())) continue;
		FString Value;
		It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Default), nullptr, Default, PPF_None);
		Lines.Add(It->GetName() + TEXT("=") + Value);
	}
#if WITH_EDITOR
	if (UAnimBlueprintGeneratedClass* AnimClass = Cast<UAnimBlueprintGeneratedClass>(Class))
	{
		for (const auto& Entry : AnimClass->GetAnimBlueprintDebugData().NodePropertyIndexToNodeMap)
		{
			const UEdGraphNode* Node = Entry.Value.Get();
			if (!Node) continue;
			Lines.Add(FString::Printf(TEXT("SourceNode PropertyIndex=%d LinkID=%d Name=%s Guid=%s Comment=%s"), Entry.Key,
				AnimClass->GetAnimNodeProperties().Num() - 1 - Entry.Key, *Node->GetName(), *Node->NodeGuid.ToString(), *Node->NodeComment));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				FString Links;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked) Links += Linked->GetOwningNode()->GetName() + TEXT(".") + Linked->PinName.ToString() + TEXT(" ");
				}
				Lines.Add(FString::Printf(TEXT("SourcePin %s.%s Direction=%d Default=%s Links=%s"), *Node->GetName(),
					*Pin->PinName.ToString(), static_cast<int32>(Pin->Direction), *Pin->DefaultValue, *Links));
			}
		}
	}
#endif
}

struct FWallPoseFixture
{
	UWorld* World = nullptr;
	UGameInstance* Instance = nullptr;
	AShooterCharacter* Shooter = nullptr;
	ARangedWeaponBase* Weapon = nullptr;
	APlayerController* Controller = nullptr;
	TArray<FString>* Diagnostics = nullptr;
	float AppliedUpAlpha = 0.0f;
	float AppliedDownAlpha = 0.0f;

	void Tick(int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		}
	}

	bool Initialize(FAutomationTestBase& Test, const TCHAR* Name)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("WallPoseAssets")), GetTransientPackage());
		if (!Test.TestNotNull(TEXT("Pose comparison world"), World)) return false;
		World->AddToRoot();
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Instance = NewObject<UGameInstance>(GEngine);
		World->SetGameInstance(Instance);
		Instance->Init();
		World->CreateAISystem();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
			TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
		const FString WeaponPath = FString::Printf(TEXT("/Game/Blueprints/Weapon/BP_%s.BP_%s_C"), Name, Name);
		UClass* WeaponClass = LoadClass<ARangedWeaponBase>(nullptr, *WeaponPath);
		if (!Test.TestNotNull(TEXT("Production Shooter"), ShooterClass)
			|| !Test.TestNotNull(TEXT("Production weapon"), WeaponClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, FVector(0, 0, 1000), FRotator::ZeroRotator, Spawn);
		Weapon = World->SpawnActor<ARangedWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Shooter"), Shooter) || !Test.TestNotNull(TEXT("Weapon"), Weapon)
			|| !Test.TestNotNull(TEXT("Controller"), Controller)) return false;
		Controller->Player = NewObject<ULocalPlayer>(GEngine);
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Shooter);
		Shooter->GetCharacterMovement()->DisableMovement();
		Shooter->SetSuitPresentation(true);
		Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
		Shooter->RefreshWeaponMode();
		Shooter->GetFirstPersonMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Weapon->GetFirstPersonWeaponMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Tick(90);
		return true;
	}

	FTransform Evaluate(const FFirstPersonProceduralAnimRuntime& Runtime)
	{
		USkeletalMeshComponent* Arms = Shooter->GetFirstPersonMesh();
		UAnimInstance* Anim = Arms->GetAnimInstance();
		FStructProperty* RuntimeProperty = FindFProperty<FStructProperty>(Anim->GetClass(), TEXT("ViewModelProceduralRuntime"));
		// NativeUpdate를 먼저 마친 다음 입력을 고정한다. 지연된 그래프 갱신이
		// 이 입력을 읽으며, 시간은 진행하지 않아 idle/몽타주 변화가 섞이지 않는다.
		Anim->UpdateAnimation(0.0f, false, UAnimInstance::EUpdateAnimationFlag::ForceParallelUpdate);
		*RuntimeProperty->ContainerPtrToValuePtr<FFirstPersonProceduralAnimRuntime>(Anim) = Runtime;
		Arms->RefreshBoneTransforms();
		TArray<UAnimInstance*> LinkedInstances;
		for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
		{
			if (!It->Struct->IsChildOf(FAnimNode_LinkedAnimGraph::StaticStruct())) continue;
			const FAnimNode_LinkedAnimGraph* Node = It->ContainerPtrToValuePtr<FAnimNode_LinkedAnimGraph>(Anim);
			if (UAnimInstance* Linked = Node->GetDynamicLinkTarget(Anim)) LinkedInstances.AddUnique(Linked);
		}
		for (UAnimInstance* Linked : LinkedInstances)
		{
			for (TFieldIterator<FStructProperty> It(Linked->GetClass()); It; ++It)
			{
				if (It->Struct == FAnimNode_ApplyAdditive::StaticStruct())
				{
					const FAnimNode_ApplyAdditive* Node = It->ContainerPtrToValuePtr<FAnimNode_ApplyAdditive>(Linked);
					if (It->GetName() == TEXT("AnimGraphNode_ApplyAdditive_1")) AppliedUpAlpha = Node->ActualAlpha;
					if (It->GetName() == TEXT("AnimGraphNode_ApplyAdditive")) AppliedDownAlpha = Node->ActualAlpha;
					if (Diagnostics) Diagnostics->Add(FString::Printf(TEXT("AppliedNode %s Alpha=%.6f ActualAlpha=%.6f"),
						*It->GetName(), Node->Alpha, Node->ActualAlpha));
				}
				if (It->Struct->IsChildOf(FAnimNode_SequenceEvaluatorBase::StaticStruct()))
				{
					const FAnimNode_SequenceEvaluatorBase* Node = It->ContainerPtrToValuePtr<FAnimNode_SequenceEvaluatorBase>(Linked);
					if (Diagnostics) Diagnostics->Add(FString::Printf(TEXT("AppliedSequence %s Sequence=%s Time=%.6f"),
						*It->GetName(), *GetPathNameSafe(Node->GetSequence()), Node->GetExplicitTime()));
				}
			}
		}
		if (Diagnostics)
		{
			for (FName Bone : {FName(TEXT("hand_r")), FName(TEXT("ik_hand_gun")), FName(TEXT("ik_hand_r")), FName(TEXT("VB hand_gun"))})
			{
				Diagnostics->Add(FString::Printf(TEXT("AppliedBone=%s Up=%.3f Down=%.3f TransformCS=%s"), *Bone.ToString(),
					Runtime.WallAvoidUpAlpha, Runtime.WallAvoidDownAlpha, *Arms->GetSocketTransform(Bone, RTS_Component).ToString()));
			}
		}
		return Weapon->GetFirstPersonWeaponMesh()->GetSocketTransform(Weapon->GetMuzzleSocketName(), RTS_World);
	}

	~FWallPoseFixture()
	{
		if (!World) return;
		if (Controller) Controller->Player = nullptr;
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(true);
		if (Instance) Instance->Shutdown();
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierWallShotPoseAssetsTest, "Outlier.Weapon.WallShot.PoseAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierWallShotPoseAssetsTest::RunTest(const FString& Parameters)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	TArray<FString> Graph;
	ExportGraph(TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FPS_ShooterArm.ABP_FPS_ShooterArm_C"), Graph);
	ExportGraph(TEXT("/Game/Characters/1P/Suit/1_Meshes/Animations/ABP_FP_ArmsProcedural.ABP_FP_ArmsProcedural_C"), Graph);
	TArray<FString> Rows;
	Rows.Add(TEXT("weapon,ads,pose,alpha,x,y,z,pitch,yaw,roll,position_delta_cm,direction_delta_degrees"));
	for (const TCHAR* Name : {TEXT("Pistol"), TEXT("Rifle")})
	{
		FWallPoseFixture Fixture;
		if (!Fixture.Initialize(*this, Name)) return false;
		Fixture.Diagnostics = &Graph;
		const UProceduralAnimValues* Values = Fixture.Weapon->GetFirstPersonProceduralValues();
		if (!TestNotNull(TEXT("Production procedural values"), Values)) return false;
		UAnimInstance* Anim = Fixture.Shooter->GetFirstPersonMesh()->GetAnimInstance();
		if (!TestNotNull(TEXT("Production FP AnimInstance"), Anim)
			|| !TestNotNull(TEXT("Controlled runtime input"), FindFProperty<FStructProperty>(Anim->GetClass(), TEXT("ViewModelProceduralRuntime")))) return false;
		TestTrue(TEXT("Production muzzle exists"), Fixture.Weapon->GetFirstPersonWeaponMesh()->DoesSocketExist(Fixture.Weapon->GetMuzzleSocketName()));
		const FWeaponValues& Config = Values->WeaponValues;
		USkeletalMeshComponent* Arms = Fixture.Shooter->GetFirstPersonMesh();
		USkeletalMeshComponent* Gun = Fixture.Weapon->GetFirstPersonWeaponMesh();
		const AShooterCharacter* Defaults = Fixture.Shooter->GetClass()->GetDefaultObject<AShooterCharacter>();
		const FString ViewFraming = FString::Printf(TEXT("ViewFraming %s Actor=%s CachedRoot=%s Root=%s CameraRelative=%s DefaultCameraRelative=%s DefaultRoot=%s Camera=%s ShotView=%s"),
			Name, *Fixture.Shooter->GetActorTransform().ToString(), *Fixture.Shooter->BaseFirstPersonCameraRootLocation.ToString(),
			*Fixture.Shooter->GetFirstPersonCameraRoot()->GetComponentTransform().ToString(),
			*Fixture.Shooter->GetFirstPersonCameraComponent()->GetRelativeTransform().ToString(),
			*Defaults->GetFirstPersonCameraComponent()->GetRelativeTransform().ToString(),
			*Defaults->GetFirstPersonCameraRoot()->GetRelativeTransform().ToString(),
			*Fixture.Shooter->GetFirstPersonCameraComponent()->GetComponentTransform().ToString(),
			*Fixture.Shooter->GetWallShotViewTransform().ToString());
		Graph.Add(ViewFraming);
		AddInfo(ViewFraming);
		FWeaponWallShotPose ShotPose;
		const bool bPoseInitialized = ShotPose.Initialize(*Fixture.Shooter, *Fixture.Weapon);
		TestTrue(TEXT("Production shot pose cache initializes: ") + ShotPose.GetDiagnostic(), bPoseInitialized);
		const auto ComparePose = [&](const TCHAR* Label, const FFirstPersonProceduralAnimRuntime& Runtime, const FTransform& Actual)
		{
			if (!bPoseInitialized) return;
			FWeaponWallAvoidanceState State;
			State.ViewModelWallOffsetLoc = Runtime.WallOffsetLoc;
			State.ViewModelWallOffsetRot = Runtime.WallOffsetRot;
			State.WallAvoidUpAlpha = Runtime.WallAvoidUpAlpha;
			State.WallAvoidDownAlpha = Runtime.WallAvoidDownAlpha;
			FTransform Evaluated;
			if (!TestTrue(TEXT("Production shot pose evaluates"), ShotPose.Evaluate(State, Config, Runtime.AimAlpha,
				Fixture.Shooter->GetWallShotViewTransform(), Evaluated))) return;
			const double Distance = FVector::Distance(Actual.GetLocation(), Evaluated.GetLocation());
			const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
				Actual.GetUnitAxis(EAxis::X), Evaluated.GetUnitAxis(EAxis::X)), -1.0, 1.0)));
			Graph.Add(FString::Printf(TEXT("ProductionComparison %s %s ADS=%.1f Up=%.1f Down=%.1f Distance=%.6f Angle=%.6f Actual=%s Evaluated=%s"),
				Name, Label, Runtime.AimAlpha, Runtime.WallAvoidUpAlpha, Runtime.WallAvoidDownAlpha, Distance, Angle,
				*Actual.ToString(), *Evaluated.ToString()));
			TestTrue(FString::Printf(TEXT("%s %s shot muzzle position matches actual controlled graph %.3fcm"), Name, Label, Distance), Distance <= 0.1);
			TestTrue(FString::Printf(TEXT("%s %s shot muzzle direction matches actual controlled graph %.3fdeg"), Name, Label, Angle), Angle <= 0.1);
		};
		Graph.Add(FString::Printf(TEXT("%s ArmsMesh=%s ArmsRelative=%s ArmsCameraRelative=%s GunRelative=%s ViewModelRootCameraRelative=%s"),
			Name, *GetPathNameSafe(Arms->GetSkeletalMeshAsset()), *Arms->GetRelativeTransform().ToString(),
			*Arms->GetComponentTransform().GetRelativeTransform(Fixture.Shooter->GetFirstPersonCameraComponent()->GetComponentTransform()).ToString(),
			*Gun->GetRelativeTransform().ToString(),
			*Fixture.Shooter->GetFirstPersonViewModelRoot()->GetComponentTransform().GetRelativeTransform(
				Fixture.Shooter->GetFirstPersonCameraComponent()->GetComponentTransform()).ToString()));
		for (FName SocketName : {FName(TEXT("HandGrip_R_Pistol")), FName(TEXT("HandGrip_R_Rifle"))})
		{
			if (const USkeletalMeshSocket* Socket = Arms->GetSkeletalMeshAsset()->FindSocket(SocketName))
			{
				Graph.Add(FString::Printf(TEXT("ArmsSocket=%s Bone=%s Local=%s"), *SocketName.ToString(),
					*Socket->BoneName.ToString(), *Socket->GetSocketLocalTransform().ToString()));
			}
		}
		if (const USkeletalMeshSocket* Socket = Gun->GetSkeletalMeshAsset()->FindSocket(Fixture.Weapon->GetMuzzleSocketName()))
		{
			Graph.Add(FString::Printf(TEXT("GunSocket=%s Bone=%s Local=%s ReferenceBoneCS=%s"), *Socket->SocketName.ToString(),
				*Socket->BoneName.ToString(), *Socket->GetSocketLocalTransform().ToString(),
				*FTransform(Gun->GetSkeletalMeshAsset()->GetComposedRefPoseMatrix(Socket->BoneName)).ToString()));
		}
		Graph.Add(FString::Printf(TEXT("%s Values=%s Up=%s Down=%s Attach=%s"), Name, *Values->GetPathName(),
			*GetPathNameSafe(Config.WallAvoidUpPose), *GetPathNameSafe(Config.WallAvoidDownPose),
			*Fixture.Shooter->GetFirstPersonWeaponSocketByType(Fixture.Weapon->GetWeaponType()).ToString()));
		for (UAnimSequenceBase* Pose : {Config.WallAvoidUpPose.Get(), Config.WallAvoidDownPose.Get()})
		{
			const UAnimSequence* Sequence = Cast<UAnimSequence>(Pose);
			if (!Sequence || !Sequence->GetSkeleton()) continue;
			Graph.Add(FString::Printf(TEXT("Sequence=%s AdditiveType=%d RefPoseType=%d RefFrame=%d RefSequence=%s Length=%.6f"),
				*Sequence->GetPathName(), static_cast<int32>(Sequence->AdditiveAnimType),
				static_cast<int32>(Sequence->RefPoseType), Sequence->RefFrameIndex,
				*GetPathNameSafe(Sequence->RefPoseSeq), Sequence->GetPlayLength()));
			const FReferenceSkeleton& Skeleton = Sequence->GetSkeleton()->GetReferenceSkeleton();
			for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
			{
				FTransform Transform;
				Sequence->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(0.0), true);
				Graph.Add(FString::Printf(TEXT("RawBone[%d] Name=%s Parent=%d Transform=%s"), Bone,
					*Skeleton.GetBoneName(Bone).ToString(), Skeleton.GetParentIndex(Bone), *Transform.ToString()));
			}
		}
		for (bool bADS : {false, true})
		{
			FFirstPersonProceduralAnimRuntime Runtime;
			Runtime.IdleIntensity = 0.0f;
			Runtime.HipPoseLoc = Config.HipPoseLoc;
			Runtime.HipPoseRot = Config.HipPoseRot;
			Runtime.WeaponRootLocOffset = Config.HipPoseLoc;
			Runtime.WeaponRootRotOffset = Config.HipPoseRot;
			Runtime.AimPoseLoc = Config.AimPoseLoc;
			Runtime.AimPoseRot = Config.AimPoseRot;
			Runtime.AimAlpha = bADS ? 1.0f : 0.0f;
			Runtime.bIsAiming = bADS;
			Runtime.WeaponPoseAlpha = 1.0f;
			Runtime.WallAvoidUpPose = Config.WallAvoidUpPose;
			Runtime.WallAvoidDownPose = Config.WallAvoidDownPose;
			const FTransform Base = Fixture.Evaluate(Runtime);
			ComparePose(TEXT("Base"), Runtime, Base);
			for (bool bUp : {true, false})
			{
				UAnimSequenceBase* Pose = bUp ? Config.WallAvoidUpPose : Config.WallAvoidDownPose;
				if (!Pose) continue; // 에셋이 없는 무기에 임의 회피 포즈를 만들지 않는다.
				for (float Alpha : {0.0f, 0.5f, 1.0f})
				{
					Runtime.WallAvoidUpAlpha = bUp ? Alpha : 0.0f;
					Runtime.WallAvoidDownAlpha = bUp ? 0.0f : Alpha;
					const FTransform Actual = Fixture.Evaluate(Runtime);
					ComparePose(bUp ? TEXT("Up") : TEXT("Down"), Runtime, Actual);
					const double Distance = FVector::Distance(Base.GetLocation(), Actual.GetLocation());
					const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
						Base.GetUnitAxis(EAxis::X), Actual.GetUnitAxis(EAxis::X)), -1.0, 1.0)));
					TestTrue(TEXT("Actual graph applied controlled up alpha"), FMath::IsNearlyEqual(Fixture.AppliedUpAlpha, Runtime.WallAvoidUpAlpha));
					TestTrue(TEXT("Actual graph applied controlled down alpha"), FMath::IsNearlyEqual(Fixture.AppliedDownAlpha, Runtime.WallAvoidDownAlpha));
					TestFalse(TEXT("Actual posed muzzle remains finite"), Actual.ContainsNaN());
					const FVector Location = Actual.GetLocation();
					const FRotator Rotation = Actual.Rotator();
					Rows.Add(FString::Printf(TEXT("%s,%d,%s,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						Name, bADS, bUp ? TEXT("Up") : TEXT("Down"), Alpha, Location.X, Location.Y, Location.Z,
						Rotation.Pitch, Rotation.Yaw, Rotation.Roll, Distance, Angle));
				}
			}
			// 회피 포즈와 별도로 root 제어의 순서도 실제 그래프에서 측정한다.
			// Hip/ADS 보정 이전 회전인지 이후 회전인지 수학식으로 가정하지 않는다.
			Runtime.WallAvoidUpAlpha = Runtime.WallAvoidDownAlpha = 0.0f;
			for (int32 Mode = 0; Mode < 4; ++Mode)
			{
				Runtime.WallOffsetLoc = Mode != 1 ? Config.WallMaxOffsetLoc : FVector::ZeroVector;
				Runtime.WallOffsetRot = Mode != 0 ? Config.WallMaxOffsetRot : FRotator::ZeroRotator;
				// 실제 WallMax 회전이 0인 에셋도 회전 합성 순서를 검증한다.
				if (Mode == 3) Runtime.WallOffsetRot = FRotator(12, -8, 17);
				const FTransform Actual = Fixture.Evaluate(Runtime);
				ComparePose(TEXT("Root"), Runtime, Actual);
				Graph.Add(FString::Printf(TEXT("RootResponse %s ADS=%d Mode=%d Loc=%s Rot=%s MuzzleWorld=%s MuzzleCS=%s RootCS=%s"),
					Name, bADS, Mode, *Runtime.WallOffsetLoc.ToString(), *Runtime.WallOffsetRot.ToString(), *Actual.ToString(),
					*Actual.GetRelativeTransform(Arms->GetComponentTransform()).ToString(),
					*Arms->GetSocketTransform(TEXT("root"), RTS_Component).ToString()));
			}
		}
		for (float Pitch : {-35.0f, 35.0f})
		{
			Fixture.Controller->SetControlRotation(FRotator(Pitch, 0, 0));
			Fixture.Tick(90);
			for (float Lean : {-1.0f, 0.0f, 1.0f})
			{
				// 충돌 해결 이후의 권위/표시 Lean을 같게 고정해 보간 지연과
				// 포즈 좌표 계산의 오차를 분리한다. 입력/충돌 해결 자체의 테스트는 아니다.
				Fixture.Shooter->CurrentLeanAlpha = Lean;
				Fixture.Shooter->PresentedLeanAlpha = Lean;
				Fixture.Shooter->ApplyLeanPresentation();
				const FTransform ShotView = Fixture.Shooter->GetWallShotViewTransform();
				TestTrue(TEXT("Resolved Lean moves shot origin exactly once"), ShotView.GetLocation().Equals(
					Fixture.Shooter->GetBaseLeanViewLocation() + Fixture.Shooter->GetLeanViewOffsetWorld(Lean), 0.001));
				TestTrue(TEXT("Shot view excludes presentation Roll"), FMath::IsNearlyZero(ShotView.Rotator().Roll, 0.001));
				for (bool bADS : {false, true})
				{
					FFirstPersonProceduralAnimRuntime Runtime;
					Runtime.IdleIntensity = 0.0f;
					Runtime.HipPoseLoc = Config.HipPoseLoc;
					Runtime.HipPoseRot = Config.HipPoseRot;
					Runtime.WeaponRootLocOffset = Config.HipPoseLoc;
					Runtime.WeaponRootRotOffset = Config.HipPoseRot;
					Runtime.AimPoseLoc = Config.AimPoseLoc;
					Runtime.AimPoseRot = Config.AimPoseRot;
					Runtime.AimAlpha = bADS ? 1.0f : 0.0f;
					Runtime.bIsAiming = bADS;
					Runtime.WeaponPoseAlpha = 1.0f;
					Runtime.WallAvoidUpPose = Config.WallAvoidUpPose;
					Runtime.WallAvoidDownPose = Config.WallAvoidDownPose;
					for (bool bWall : {false, true})
					{
						Runtime.WallOffsetLoc = bWall ? Config.WallMaxOffsetLoc : FVector::ZeroVector;
						Runtime.WallOffsetRot = bWall ? Config.WallMaxOffsetRot : FRotator::ZeroRotator;
						const FTransform Actual = Fixture.Evaluate(Runtime);
						const FString Label = FString::Printf(TEXT("Pitch%.0fLean%.0fWall%d"), Pitch, Lean, bWall);
						ComparePose(*Label, Runtime, Actual);
						const FFloatProperty* AimPitchProperty = FindFProperty<FFloatProperty>(Anim->GetClass(), TEXT("AimPitch"));
						Graph.Add(FString::Printf(TEXT("PitchResponse %s Pitch=%.1f ADS=%d Wall=%d AnimAimPitch=%.6f Camera=%s ArmsCameraRelative=%s MuzzleWorld=%s"),
							Name, Pitch, bADS, bWall, AimPitchProperty ? AimPitchProperty->GetPropertyValue_InContainer(Anim) : 0.0f,
							*Fixture.Shooter->GetFirstPersonCameraComponent()->GetComponentTransform().ToString(),
							*Arms->GetComponentTransform().GetRelativeTransform(Fixture.Shooter->GetFirstPersonCameraComponent()->GetComponentTransform()).ToString(),
							*Actual.ToString()));
					}
				}
			}
		}
		Fixture.Shooter->CurrentLeanAlpha = 0.0f;
		Fixture.Shooter->PresentedLeanAlpha = 0.0f;
		Fixture.Shooter->ApplyLeanPresentation();
		Fixture.Controller->SetControlRotation(FRotator::ZeroRotator);
		Fixture.Tick(1);
		for (float Lean : {-1.0f, 1.0f})
		{
			Fixture.Shooter->SetLeanTarget(Lean, false);
			Fixture.Tick(120);
			TestTrue(TEXT("Production Lean timer reaches requested side in open space"),
				FMath::IsNearlyEqual(Fixture.Shooter->GetCurrentLeanAlpha(), Lean, 0.001f));
		}
		Fixture.Shooter->SetLeanTarget(0.0f, false);
		Fixture.Tick(120);
		AActor* LeanBlockerActor = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* LeanBlocker = NewObject<UBoxComponent>(LeanBlockerActor);
		LeanBlockerActor->SetRootComponent(LeanBlocker);
		LeanBlocker->SetBoxExtent(FVector(100, 1, 100));
		LeanBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		LeanBlocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		LeanBlocker->RegisterComponent();
		LeanBlocker->SetWorldLocation(Fixture.Shooter->GetBaseLeanViewLocation()
			+ FVector(0, Fixture.Shooter->LeanCollisionRadius + 2.0f, 0));
		LeanBlocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Fixture.Shooter->SetLeanTarget(1.0f, false);
		Fixture.Tick(120);
		TestTrue(TEXT("Production Lean timer is collision constrained"),
			Fixture.Shooter->GetCurrentLeanAlpha() >= 0.0f && Fixture.Shooter->GetCurrentLeanAlpha() < 0.5f);
		LeanBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fixture.Tick(120);
		TestTrue(TEXT("Production Lean timer recovers after blocker removal"),
			FMath::IsNearlyEqual(Fixture.Shooter->GetCurrentLeanAlpha(), 1.0f, 0.001f));
		Fixture.Shooter->SetLeanTarget(0.0f, false);
		Fixture.Tick(120);

		const bool bActorBeforeArms = Arms->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
			[&](const FTickPrerequisite& Prerequisite) { return Prerequisite.Get() == &Fixture.Shooter->PrimaryActorTick; });
		TestTrue(TEXT("Actor wall update precedes FP animation consumer"), bActorBeforeArms);
		Arms->SetComponentTickEnabled(false);
		Gun->SetComponentTickEnabled(false);
		const FTransform View = Fixture.Shooter->GetWallShotViewTransform();
		AActor* WallActor = Fixture.World->SpawnActor<AActor>();
		UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor);
		WallActor->SetRootComponent(Wall);
		Wall->SetBoxExtent(FVector(2, 500, 500));
		Wall->SetCollisionResponseToAllChannels(ECR_Block);
		Wall->RegisterComponent();
		Wall->SetWorldLocation(View.GetLocation() + FVector(45, 0, 0));
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Fixture.Tick(90);
		const FWeaponWallAvoidanceState& State = Fixture.Shooter->GetWeaponWallAvoidanceState();
		TestTrue(TEXT("Wall state approaches and holds without FP animation or firing"), State.GetShotBlendAlpha() > 0.0f);
		const float BeforeReads = State.GetShotBlendAlpha();
		// PlayerState의 늦은 도착으로 동일 외형을 다시 연결해도 현재 장착의 보정은 유효해야 한다.
		const uint32 BeforeRefreshRevision = Fixture.Shooter->GetWallShotEquipmentRevision();
		FWeaponWallAvoidanceSnapshot CurrentSnapshot;
		CurrentSnapshot.Weapon = Fixture.Weapon;
		CurrentSnapshot.EquipmentRevision = BeforeRefreshRevision;
		Fixture.Shooter->RefreshReplicatedPresentation();
		TestEqual(TEXT("Same presentation refresh preserves equipment revision"),
			Fixture.Shooter->GetWallShotEquipmentRevision(), BeforeRefreshRevision);
		TestEqual(TEXT("Same presentation refresh preserves active avoidance"), State.GetShotBlendAlpha(), BeforeReads);
		TestTrue(TEXT("Same presentation refresh preserves ready weapon cache"),
			Fixture.Shooter->WallAvoidanceWeapon.Get() == Fixture.Weapon);
		TestTrue(TEXT("Same presentation refresh permits current revision correction"), CurrentSnapshot.MatchesEquipment(
			Fixture.Weapon, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
			Fixture.Shooter->InvalidatedWallWeaponRevision));
		FTransform FirstRead;
		const FTransform ControlledMuzzle(FRotator(30, 0, 0), View.GetLocation() + FVector(20, 0, 0));
		TestTrue(TEXT("Controlled shot transform resolves"), State.ResolveShotTransform(View, ControlledMuzzle, FirstRead));
		for (int32 Read = 0; Read < 100; ++Read)
		{
			FTransform RepeatedRead;
			TestTrue(TEXT("Repeated shot transform resolves"), State.ResolveShotTransform(View, ControlledMuzzle, RepeatedRead));
			TestTrue(TEXT("Same-frame transform reads are stable"), RepeatedRead.Equals(FirstRead, 1.e-6));
		}
		TestEqual(TEXT("Same-frame reads do not advance blend"), State.GetShotBlendAlpha(), BeforeReads);
		FTransform ProductionRead;
		TestTrue(TEXT("Character shot query resolves with FP and weapon ticks disabled"), Fixture.Shooter->TryGetWallShotTransform(ProductionRead));
		for (int32 Read = 0; Read < 100; ++Read)
		{
			FTransform RepeatedRead;
			TestTrue(TEXT("Repeated character shot query resolves"), Fixture.Shooter->TryGetWallShotTransform(RepeatedRead));
			TestTrue(TEXT("Character shot queries remain stable without mesh updates"), RepeatedRead.Equals(ProductionRead, 1.e-6));
		}
		TestEqual(TEXT("Character shot queries do not advance blend"), State.GetShotBlendAlpha(), BeforeReads);
		// 복제된 외형/세대의 늦은 도착은 로컬 캐시만 다시 만든다. 실제 재장착과 달리
		// 서버가 이미 보낸 현재 세대의 스냅샷을 무효화하면 다음 보정이 영구 차단된다.
		Fixture.Shooter->ResetWeaponWallAvoidance(false);
		TestEqual(TEXT("Replication catch-up preserves authoritative revision"),
			Fixture.Shooter->GetWallShotEquipmentRevision(), BeforeRefreshRevision);
		TestTrue(TEXT("Replication catch-up permits current snapshot"), CurrentSnapshot.MatchesEquipment(
			Fixture.Weapon, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
			Fixture.Shooter->InvalidatedWallWeaponRevision));
		Fixture.Tick(1);
		TestTrue(TEXT("Replication catch-up rebuilds current weapon cache"),
			Fixture.Shooter->WallAvoidanceWeapon.Get() == Fixture.Weapon);
		TestTrue(TEXT("Rebuilt cache still permits current snapshot"), CurrentSnapshot.MatchesEquipment(
			Fixture.Weapon, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
			Fixture.Shooter->InvalidatedWallWeaponRevision));
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fixture.Tick(300);
		TestEqual(TEXT("Wall state releases with FP animation disabled"), State.GetShotBlendAlpha(), 0.0f);
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Fixture.Tick(90);
		TestTrue(TEXT("Wall state enters again before unequip"), State.GetShotBlendAlpha() > 0.0f);
		Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(nullptr);
		Fixture.Shooter->RefreshWeaponMode();
		Fixture.Tick(1);
		TestEqual(TEXT("Unequip resets previous weapon wall state"), State.GetShotBlendAlpha(), 0.0f);
		FTransform UnequippedShot;
		TestFalse(TEXT("Unequipped production character cannot resolve a shot"), Fixture.Shooter->TryGetWallShotTransform(UnequippedShot));
		TestFalse(TEXT("Unequip rejects previous equipment snapshot"), CurrentSnapshot.MatchesEquipment(
			Fixture.Shooter->GetCurrentWeapon(), Fixture.Shooter->GetWallShotEquipmentRevision(),
			Fixture.Shooter->InvalidatedWallWeapon.Get(), Fixture.Shooter->InvalidatedWallWeaponRevision));
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fixture.Shooter->AFirstPersonCharacter::EquipWeapon(Fixture.Weapon);
		Fixture.Shooter->RefreshWeaponMode();
		Fixture.Tick(1);
		TestEqual(TEXT("Equip in open space does not restore previous avoidance"), State.GetShotBlendAlpha(), 0.0f);
		TestFalse(TEXT("Same actor re-equip rejects previous equipment snapshot"), CurrentSnapshot.MatchesEquipment(
			Fixture.Weapon, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
			Fixture.Shooter->InvalidatedWallWeaponRevision));
		FTransform ReequippedShot;
		TestTrue(TEXT("Re-equipped production character resolves a shot"), Fixture.Shooter->TryGetWallShotTransform(ReequippedShot));
		TestTrue(TEXT("Re-equipped open-space shot returns exactly to authoritative view"),
			ReequippedShot.Equals(Fixture.Shooter->GetWallShotViewTransform(), 0.001));
		FOutlierLoadoutSnapshot RespawnLoadout;
		const bool bTestInventoryLifecycle = Fixture.Weapon->GetWeaponType() == EWeaponType::Rifle;
		if (bTestInventoryLifecycle)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ARangedWeaponBase* InventoryWeapon = Fixture.World->SpawnActor<ARangedWeaponBase>(Fixture.Weapon->GetClass(),
				FVector(0, 2000, 1000), FRotator::ZeroRotator, Spawn);
			ARangedWeaponBase* Replacement = Fixture.World->SpawnActor<ARangedWeaponBase>(Fixture.Weapon->GetClass(),
				FVector(0, 2200, 1000), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("Production inventory rifle"), InventoryWeapon)
				|| !TestNotNull(TEXT("Production replacement rifle"), Replacement)) return false;
			Fixture.Shooter->GetInventoryComponent()->HandleEquipWeapon(InventoryWeapon);
			Fixture.Tick(180);
			TestTrue(TEXT("Production pickup populates primary slot"),
				Fixture.Shooter->GetInventoryComponent()->GetWeaponInSlot(EWeaponSlot::Primary) == InventoryWeapon);
			Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Fixture.Tick(90);
			TestTrue(TEXT("Inventory weapon has active avoidance before replacement"), State.GetShotBlendAlpha() > 0.0f);
			FWeaponWallAvoidanceSnapshot DroppedSnapshot;
			DroppedSnapshot.Weapon = InventoryWeapon;
			DroppedSnapshot.EquipmentRevision = Fixture.Shooter->GetWallShotEquipmentRevision();
			Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Fixture.Shooter->GetInventoryComponent()->HandleEquipWeapon(Replacement);
			TestTrue(TEXT("Production replacement drops previous rifle owner"), InventoryWeapon->GetOwner() == nullptr);
			Fixture.Tick(180);
			TestTrue(TEXT("Production replacement equips new rifle"), Fixture.Shooter->GetCurrentWeapon() == Replacement);
			TestTrue(TEXT("Replacement rebuilds cache for new rifle"), Fixture.Shooter->WallAvoidanceWeapon.Get() == Replacement);
			TestEqual(TEXT("Replacement does not inherit previous avoidance"), State.GetShotBlendAlpha(), 0.0f);
			TestFalse(TEXT("Production Drop rejects previous rifle snapshot"), DroppedSnapshot.MatchesEquipment(
				Replacement, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
				Fixture.Shooter->InvalidatedWallWeaponRevision));
			Fixture.Weapon = Replacement;
			Fixture.Shooter->GetInventoryComponent()->BuildLoadoutSnapshot(RespawnLoadout, true);
		}
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Fixture.Tick(90);
		TestTrue(TEXT("Production avoidance is active before death"), State.GetShotBlendAlpha() > 0.0f);
		FWeaponWallAvoidanceSnapshot BeforeDeathSnapshot;
		BeforeDeathSnapshot.Weapon = Fixture.Weapon;
		BeforeDeathSnapshot.EquipmentRevision = Fixture.Shooter->GetWallShotEquipmentRevision();
		Fixture.Shooter->Die();
		TestTrue(TEXT("Production death applies dead state"), Fixture.Shooter->IsDead());
		TestEqual(TEXT("Production death clears active avoidance"), State.GetShotBlendAlpha(), 0.0f);
		FTransform DeadShot;
		TestFalse(TEXT("Dead production character cannot resolve a shot"), Fixture.Shooter->TryGetWallShotTransform(DeadShot));
		TestFalse(TEXT("Production death rejects previous equipment snapshot"), BeforeDeathSnapshot.MatchesEquipment(
			Fixture.Weapon, Fixture.Shooter->GetWallShotEquipmentRevision(), Fixture.Shooter->InvalidatedWallWeapon.Get(),
			Fixture.Shooter->InvalidatedWallWeaponRevision));
		if (bTestInventoryLifecycle)
		{
			// 실제 새 Pawn과 인벤토리 복원 경로만 검증한다. GameMode 재시작/맵 이동은 포함하지 않는다.
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AShooterCharacter* Restored = Fixture.World->SpawnActor<AShooterCharacter>(Fixture.Shooter->GetClass(),
				FVector(0, 4000, 1000), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("New production pawn for loadout restoration"), Restored)) return false;
			Restored->GetCharacterMovement()->DisableMovement();
			Restored->SetSuitPresentation(true);
			Restored->GetInventoryComponent()->RestoreLoadout(RespawnLoadout, true);
			Fixture.Tick(90);
			TestFalse(TEXT("Restored production pawn starts alive"), Restored->IsDead());
			TestTrue(TEXT("Loadout restoration creates a new rifle actor"), Restored->GetCurrentWeapon()
				&& Restored->GetCurrentWeapon() != Fixture.Weapon
				&& Restored->GetCurrentWeapon()->GetClass() == Fixture.Weapon->GetClass());
			TestEqual(TEXT("Restored pawn has no previous avoidance"), Restored->GetWeaponWallAvoidanceState().GetShotBlendAlpha(), 0.0f);
			FTransform RestoredShot;
			TestTrue(TEXT("Restored loadout resolves a production shot"), Restored->TryGetWallShotTransform(RestoredShot));
			TestTrue(TEXT("Restored open-space shot equals new pawn view"), RestoredShot.Equals(Restored->GetWallShotViewTransform(), 0.001));
			TestFalse(TEXT("Restored pawn rejects previous life snapshot"), BeforeDeathSnapshot.MatchesEquipment(
				Restored->GetCurrentWeapon(), Restored->GetWallShotEquipmentRevision(), Restored->InvalidatedWallWeapon.Get(),
				Restored->InvalidatedWallWeaponRevision));
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("WallShotValidation");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Pose comparison CSV written"), FFileHelper::SaveStringArrayToFile(Rows, *(Directory / TEXT("PoseAssets.csv"))));
	TestTrue(TEXT("Actual graph links exported"), FFileHelper::SaveStringArrayToFile(Graph, *(Directory / TEXT("PoseGraph.txt"))));
	return !HasAnyErrors();
}

#endif
