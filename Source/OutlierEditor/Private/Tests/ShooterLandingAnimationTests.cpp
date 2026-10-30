#if WITH_DEV_AUTOMATION_TESTS

#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterAnimInstance.h"
#include "Shooter/ShooterFirstPersonAnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_SaveCachedPose.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_TransitionResult.h"
#include "Animation/AnimNode_UseCachedPose.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "Weapon/WeaponBase.h"

namespace ShooterLandingAnimationTests
{
bool IsAirState(FName State)
{
	// JumpEnd는 지상 착지 표현이다. 공중 논리의 종료와 착지 블렌드 종료를 구분한다.
	static const FName JumpStart(TEXT("JumpStart"));
	static const FName Jumping(TEXT("Jumping"));
	return State == JumpStart || State == Jumping;
}

struct FGraphProbe
{
	UAnimInstance* Instance;
	TArray<int32> Machines;
	TSet<int32> ObservedAirMachines;
	bool bHasAirStates = false;
	TMap<int32, TPair<int32, float>> PreviousMachineTimes;
	int32 TimeAdvances = 0;
	int32 TimeStalls = 0;
	int32 TimeRegressions = 0;
	int32 StateChanges = 0;
	int32 CounterStalls = 0;
	int32 InstanceChanges = 0;
	int32 PreviousUpdateCounter = INDEX_NONE;
	bool bHasPreviousUpdate = false;

	explicit FGraphProbe(UAnimInstance* InInstance) : Instance(InInstance)
	{
		if (const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass()))
		{
			// 런타임 API의 인덱스는 baked machine 번호가 아니라 역순 AnimNode property 번호다.
			// 이름이 같은 무기별 Locomotion도 각각 추적해 다른 머신을 검사하는 오류를 피한다.
			for (int32 Index = 0; Index < Class->GetAnimNodeProperties().Num(); ++Index)
			{
				const FAnimNode_StateMachine* Machine = Instance->GetStateMachineInstance(Index);
				if (!Machine) continue;
				Machines.Add(Index);
				const TArray<FBakedAnimationStateMachine>& Descriptions = Class->GetBakedStateMachines();
				if (Descriptions.IsValidIndex(Machine->StateMachineIndexInClass))
				{
					for (const FBakedAnimationState& State : Descriptions[Machine->StateMachineIndexInClass].States)
					{
						bHasAirStates |= IsAirState(State.StateName);
					}
				}
			}
		}
	}

	const FBakedAnimationStateMachine* FindMachineDescription(const FAnimNode_StateMachine& Machine) const
	{
		// 노드 내부의 protected getter 대신 공개된 컴파일 정보와 해당 노드의 baked 인덱스를 사용한다.
		const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (!Class) return nullptr;
		const TArray<FBakedAnimationStateMachine>& Descriptions = Class->GetBakedStateMachines();
		return Descriptions.IsValidIndex(Machine.StateMachineIndexInClass)
			? &Descriptions[Machine.StateMachineIndexInClass] : nullptr;
	}

	const FStructProperty* FindNodeProperty(int32 NodeIndex) const
	{
		const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (!Class) return nullptr;
		const TArray<FStructProperty*>& Properties = Class->GetAnimNodeProperties();
		const int32 PropertyIndex = Properties.Num() - 1 - NodeIndex;
		return Properties.IsValidIndex(PropertyIndex) ? Properties[PropertyIndex] : nullptr;
	}

	void SampleGroundedFrame(const USkeletalMeshComponent* Mesh)
	{
		// 판정을 바꾸지 않고 모든 착지 프레임을 집계한다. 경과 시간 감소는 초기화/재진입의 단서이며 확정 판정은 아니다.
		InstanceChanges += Mesh->GetAnimInstance() != Instance ? 1 : 0;
		const int32 Counter = Instance->GetUpdateCounter().Get();
		if (bHasPreviousUpdate && Counter == PreviousUpdateCounter) ++CounterStalls;
		PreviousUpdateCounter = Counter;
		bHasPreviousUpdate = true;
		for (int32 Index : ObservedAirMachines)
		{
			const FAnimNode_StateMachine* Machine = Instance->GetStateMachineInstance(Index);
			const TPair<int32, float> Current(Machine->GetCurrentState(), Machine->GetCurrentStateElapsedTime());
			if (const TPair<int32, float>* Previous = PreviousMachineTimes.Find(Index))
			{
				if (Current.Key != Previous->Key) ++StateChanges;
				else if (Current.Value > Previous->Value + SMALL_NUMBER) ++TimeAdvances;
				else if (Current.Value < Previous->Value - SMALL_NUMBER) ++TimeRegressions;
				else ++TimeStalls;
			}
			PreviousMachineTimes.Add(Index, Current);
		}
	}

	FString DescribeFrameSummary() const
	{
		return FString::Printf(TEXT("TimeAdvances=%d TimeStalls=%d SameStateTimeRegressions=%d StateChanges=%d InstanceCounterStalls=%d InstanceChanges=%d"),
			TimeAdvances, TimeStalls, TimeRegressions, StateChanges, CounterStalls, InstanceChanges);
	}

	FString DescribeEvaluation(const USkeletalMeshComponent* Mesh) const
	{
		FString Result = FString::Printf(TEXT("Instance=%s SameInstance=%d Counter=%d Delta=%.6f RateScale=%.6f Paused=%d Enabled=%d NoSkeletonUpdate=%d URO=%d VisibilityTick=%d MultiThread=%d WorldTime=%.6f WorldDelta=%.6f Linked=%d PostProcess=%s"),
			*Instance->GetPathName(), Mesh->GetAnimInstance() == Instance ? 1 : 0, Instance->GetUpdateCounter().Get(),
			Instance->GetDeltaSeconds(), Mesh->GlobalAnimRateScale, Mesh->bPauseAnims ? 1 : 0, Mesh->bEnableAnimation ? 1 : 0,
			Mesh->bNoSkeletonUpdate ? 1 : 0, Mesh->bEnableUpdateRateOptimizations ? 1 : 0,
			static_cast<int32>(Mesh->VisibilityBasedAnimTickOption), Instance->bUseMultiThreadedAnimationUpdate ? 1 : 0,
			Mesh->GetWorld()->GetTimeSeconds(), Mesh->GetWorld()->GetDeltaSeconds(), Mesh->GetLinkedAnimInstances().Num(),
			*GetPathNameSafe(Mesh->GetPostProcessInstance()));
		for (const FName Flag : {FName(TEXT("bIsInAir")), FName(TEXT("bIsGrounded"))})
		{
			const FBoolProperty* Property = FindFProperty<FBoolProperty>(Instance->GetClass(), Flag);
			Result += FString::Printf(TEXT(" %s=%d"), *Flag.ToString(), Property ? (Property->GetPropertyValue_InContainer(Instance) ? 1 : 0) : -1);
		}
		for (const UAnimInstance* Linked : Mesh->GetLinkedAnimInstances())
		{
			if (!Linked) continue;
			Result += FString::Printf(TEXT(" {Linked=%s Counter=%d Delta=%.6f}"),
				*Linked->GetPathName(), Linked->GetUpdateCounter().Get(), Linked->GetDeltaSeconds());
		}
		const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (Class)
		{
			for (int32 Index = 0; Index < Class->GetAnimNodeProperties().Num(); ++Index)
			{
				const FStructProperty* Property = FindNodeProperty(Index);
				if (!Property->Struct->IsChildOf(FAnimNode_SaveCachedPose::StaticStruct())) continue;
				const auto* Cache = Property->ContainerPtrToValuePtr<FAnimNode_SaveCachedPose>(Instance);
				Result += FString::Printf(TEXT(" {Cache=%d:%s Weight=%.6f}"), Index, *Cache->CachePoseName.ToString(), Cache->GlobalWeight);
			}
		}
		return Result;
	}

	TArray<FString> DescribePoseLinks() const
	{
		TArray<FString> Nodes;
		const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (!Class) return {TEXT("MissingAnimClass")};
		// 캐시 포즈의 공유/오연결을 찾을 수 있도록 노드 번호와 실제 직렬화된 PoseLink를 함께 남긴다.
		// 링크를 실행하거나 재초기화하지 않으며, private 런타임 카운터에도 접근하지 않는다.
		for (int32 Index = 0; Index < Class->GetAnimNodeProperties().Num(); ++Index)
		{
			const FStructProperty* Property = FindNodeProperty(Index);
			const void* Node = Property->ContainerPtrToValuePtr<void>(Instance);
			FString Result = FString::Printf(TEXT("[%d:%s/%s"), Index, *Property->GetName(), *Property->Struct->GetName());
			if (Property->Struct->IsChildOf(FAnimNode_SaveCachedPose::StaticStruct()))
			{
				const auto* Cache = static_cast<const FAnimNode_SaveCachedPose*>(Node);
				Result += FString::Printf(TEXT(" Save=%s GlobalWeight=%.6f"), *Cache->CachePoseName.ToString(), Cache->GlobalWeight);
			}
			if (Property->Struct->IsChildOf(FAnimNode_UseCachedPose::StaticStruct()))
			{
				const auto* Cache = static_cast<const FAnimNode_UseCachedPose*>(Node);
				Result += FString::Printf(TEXT(" Use=%s"), *Cache->CachePoseName.ToString());
			}
			for (TFieldIterator<FProperty> Field(Property->Struct); Field; ++Field)
			{
				if (const FStructProperty* Link = CastField<FStructProperty>(*Field))
				{
					if (Link->Struct->IsChildOf(FPoseLinkBase::StaticStruct()))
					{
						Result += FString::Printf(TEXT(" %s->%d"), *Link->GetName(), Link->ContainerPtrToValuePtr<FPoseLinkBase>(Node)->LinkID);
					}
				}
				else if (const FArrayProperty* Array = CastField<FArrayProperty>(*Field))
				{
					const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner);
					if (!Inner || !Inner->Struct->IsChildOf(FPoseLinkBase::StaticStruct())) continue;
					FScriptArrayHelper Links(Array, Array->ContainerPtrToValuePtr<void>(Node));
					for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
					{
						Result += FString::Printf(TEXT(" %s[%d]->%d"), *Array->GetName(), LinkIndex,
							reinterpret_cast<const FPoseLinkBase*>(Links.GetRawPtr(LinkIndex))->LinkID);
					}
				}
			}
			Result += TEXT("]");
			Nodes.Add(MoveTemp(Result));
		}
		return Nodes;
	}

	bool HasActiveMachine() const
	{
		for (int32 Index : Machines)
		{
			if (Instance->GetInstanceMachineWeight(Index) > KINDA_SMALL_NUMBER) return true;
		}
		return false;
	}

	void ObserveAir()
	{
		for (int32 Index : Machines)
		{
			if (Instance->GetInstanceMachineWeight(Index) > KINDA_SMALL_NUMBER
				&& IsAirState(Instance->GetCurrentStateName(Index)))
			{
				ObservedAirMachines.Add(Index);
			}
		}
	}

	bool HasCurrentAirState() const
	{
		for (int32 Index : Machines)
		{
			if ((ObservedAirMachines.Contains(Index) || Instance->GetInstanceMachineWeight(Index) > KINDA_SMALL_NUMBER)
				&& IsAirState(Instance->GetCurrentStateName(Index))) return true;
		}
		return false;
	}

	bool HasLandingState() const
	{
		static const FName JumpEnd(TEXT("JumpEnd"));
		for (int32 Index : ObservedAirMachines)
		{
			if (Instance->GetCurrentStateName(Index) == JumpEnd) return true;
		}
		return false;
	}

	float GetAirWeight() const
	{
		float Weight = 0.0f;
		for (int32 Index : ObservedAirMachines)
		{
			const FAnimNode_StateMachine* Machine = Instance->GetStateMachineInstance(Index);
			const FBakedAnimationStateMachine* Description = FindMachineDescription(*Machine);
			if (!Description) return 1.0f;
			for (int32 StateIndex = 0; StateIndex < Description->States.Num(); ++StateIndex)
			{
				if (IsAirState(Description->States[StateIndex].StateName))
				{
					Weight = FMath::Max(Weight, Machine->GetStateWeight(StateIndex));
				}
			}
		}
		return Weight;
	}

	FString Describe() const
	{
		FString Result;
		for (int32 Index : Machines)
		{
			if (Instance->GetInstanceMachineWeight(Index) <= KINDA_SMALL_NUMBER
				&& !ObservedAirMachines.Contains(Index)) continue;
			const FAnimNode_StateMachine* Machine = Instance->GetStateMachineInstance(Index);
			const FBakedAnimationStateMachine* Description = FindMachineDescription(*Machine);
			Result += FString::Printf(TEXT("[%d:%s/%s Weight=%.3f Elapsed=%.6f Reinit=%d SkipFirstTransition=%d MaxTransitions=%d]"), Index,
				Description ? *Description->MachineName.ToString() : TEXT("InvalidMachine"), *Machine->GetCurrentStateName().ToString(),
				Instance->GetInstanceMachineWeight(Index), Machine->GetCurrentStateElapsedTime(),
				Machine->bReinitializeOnBecomingRelevant ? 1 : 0, Machine->bSkipFirstUpdateTransition ? 1 : 0, Machine->MaxTransitionsPerFrame);
		}
		return Result;
	}

	FString DescribeLandingTiming() const
	{
		const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Instance->GetClass());
		if (!Class) return TEXT("MissingAnimClass");
		const TArray<FStructProperty*>& Properties = Class->GetAnimNodeProperties();
		FString Result;
		for (int32 Index : ObservedAirMachines)
		{
			const FAnimNode_StateMachine* Machine = Instance->GetStateMachineInstance(Index);
			const FBakedAnimationStateMachine* Description = FindMachineDescription(*Machine);
			if (!Description)
			{
				Result += FString::Printf(TEXT("[Machine=%d MissingDescription]"), Index);
				continue;
			}
			for (int32 StateIndex = 0; StateIndex < Description->States.Num(); ++StateIndex)
			{
				const FBakedAnimationState& State = Description->States[StateIndex];
				if (State.StateName != FName(TEXT("JumpEnd"))) continue;
				// 현재 상태의 시계와 JumpEnd 플레이어의 시계를 따로 읽어, 전이 정체와 재생 정체를 구분한다.
				// Relevant getter의 FLT_MAX(관련 플레이어 없음)도 숨기지 않고 그대로 기록한다.
				Result += FString::Printf(TEXT("[Machine=%d:%s Baked=%d Current=%s CurrentElapsed=%.6f JumpEndWeight=%.6f RelevantTime=%g RelevantLength=%g RelevantRemaining=%g Players=%d State=%d Root=%d Conduit=%d ResetOnEntry=%d Layers=%d"),
					Index, *Description->MachineName.ToString(), Machine->StateMachineIndexInClass,
					*Machine->GetCurrentStateName().ToString(), Machine->GetCurrentStateElapsedTime(),
					Machine->GetStateWeight(StateIndex), Instance->GetRelevantAnimTime(Index, StateIndex),
					Instance->GetRelevantAnimLength(Index, StateIndex), Instance->GetRelevantAnimTimeRemaining(Index, StateIndex),
					State.PlayerNodeIndices.Num(), StateIndex, State.StateRootNodeIndex, State.bIsAConduit ? 1 : 0,
					State.bAlwaysResetOnEntry ? 1 : 0, State.LayerNodeIndices.Num());
				for (int32 OtherState = 0; OtherState < Description->States.Num(); ++OtherState)
				{
					Result += FString::Printf(TEXT(" {State=%d:%s Weight=%.6f Root=%d}"), OtherState,
						*Description->States[OtherState].StateName.ToString(), Machine->GetStateWeight(OtherState),
						Description->States[OtherState].StateRootNodeIndex);
				}
				for (int32 TransitionIndex = 0; TransitionIndex < Description->Transitions.Num(); ++TransitionIndex)
				{
					if (!Machine->IsTransitionActive(TransitionIndex)) continue;
					const FAnimationTransitionBetweenStates& Active = Description->Transitions[TransitionIndex];
					Result += FString::Printf(TEXT(" {ActiveTransition=%d From=%d To=%d Blend=%.6f Elapsed=%.6f Fraction=%.6f Logic=%d}"),
						TransitionIndex, Active.PreviousState, Active.NextState, Active.CrossfadeDuration,
						Instance->GetInstanceTransitionTimeElapsed(Index, TransitionIndex),
						Instance->GetInstanceTransitionTimeElapsedFraction(Index, TransitionIndex), static_cast<int32>(Active.LogicType.GetValue()));
				}
				for (const FBakedStateExitTransition& Exit : State.Transitions)
				{
					if (!Description->Transitions.IsValidIndex(Exit.TransitionIndex))
					{
						Result += TEXT(" {InvalidExitTransition}");
						continue;
					}
					const FAnimationTransitionBetweenStates& Transition = Description->Transitions[Exit.TransitionIndex];
					const FStructProperty* RuleProperty = FindNodeProperty(Exit.CanTakeDelegateIndex);
					const FAnimNode_TransitionResult* Rule = RuleProperty && RuleProperty->Struct->IsChildOf(FAnimNode_TransitionResult::StaticStruct())
						? RuleProperty->ContainerPtrToValuePtr<FAnimNode_TransitionResult>(Instance) : nullptr;
					// bCanEnterTransition은 마지막 평가 결과다. 로그 수집이 전이 규칙을 재실행하지 않도록 한다.
					Result += FString::Printf(TEXT(" {Exit=%d ToState=%d Active=%d Blend=%.6f Logic=%d RuleNode=%d LastRule=%d Desired=%d Auto=%d Trigger=%.6f}"),
						Exit.TransitionIndex, Transition.NextState, Machine->IsTransitionActive(Exit.TransitionIndex) ? 1 : 0,
						Transition.CrossfadeDuration, static_cast<int32>(Transition.LogicType.GetValue()), Exit.CanTakeDelegateIndex,
						Rule ? (Rule->bCanEnterTransition ? 1 : 0) : -1, Exit.bDesiredTransitionReturnValue ? 1 : 0,
						Exit.bAutomaticRemainingTimeRule ? 1 : 0, Exit.AutomaticRuleTriggerTime);
				}
				for (int32 PlayerIndex : State.PlayerNodeIndices)
				{
					// baked PlayerNodeIndices도 역순 property 인덱스다. 이름 검색 대신 실제 상태의 노드를 읽는다.
					const int32 PropertyIndex = Properties.Num() - 1 - PlayerIndex;
					const FStructProperty* Property = Properties.IsValidIndex(PropertyIndex) ? Properties[PropertyIndex] : nullptr;
					if (!Property || !Property->Struct->IsChildOf(FAnimNode_AssetPlayerRelevancyBase::StaticStruct()))
					{
						Result += FString::Printf(TEXT(" {Player=%d MissingPlayer}"), PlayerIndex);
						continue;
					}
					const auto* Player = Property->ContainerPtrToValuePtr<FAnimNode_AssetPlayerRelevancyBase>(Instance);
					Result += FString::Printf(TEXT(" {Player=%d Node=%s Asset=%s Time=%.6f GetterTime=%.6f Length=%.6f Remaining=%.6f Weight=%.6f Loop=%d IgnoreRelevancy=%d}"),
						PlayerIndex, *Property->GetName(), *GetPathNameSafe(Player->GetAnimAsset()),
						Player->GetAccumulatedTime(), Instance->GetInstanceAssetPlayerTime(PlayerIndex),
						Instance->GetInstanceAssetPlayerLength(PlayerIndex), Instance->GetInstanceAssetPlayerTimeFromEnd(PlayerIndex),
						Player->GetCachedBlendWeight(), Player->IsLooping() ? 1 : 0, Player->GetIgnoreForRelevancyTest() ? 1 : 0);
					if (Property->Struct->IsChildOf(FAnimNode_SequencePlayer::StaticStruct()))
					{
						const auto* Sequence = Property->ContainerPtrToValuePtr<FAnimNode_SequencePlayer>(Instance);
						Result += FString::Printf(TEXT(" {PlayRate=%.6f RateBasis=%.6f Start=%.6f AssetRateScale=%.6f SyncGroup=%s SyncMethod=%d SyncRole=%d}"),
							Sequence->GetPlayRate(), Sequence->GetPlayRateBasis(), Sequence->GetStartPosition(),
							Sequence->GetSequence() ? Sequence->GetSequence()->RateScale : 0.0f,
							*Sequence->GetGroupName().ToString(), static_cast<int32>(Sequence->GetGroupMethod()), static_cast<int32>(Sequence->GetGroupRole()));
					}
				}
				Result += TEXT("]");
			}
		}
		return Result.IsEmpty() ? TEXT("NoObservedLandingState") : Result;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierShooterLandingAnimationTest,
	"Outlier.Animation.Shooter.LandingState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierShooterLandingAnimationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	const TCHAR* WeaponPaths[] = {
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"),
		TEXT("/Game/Blueprints/Weapon/BP_Hammer.BP_Hammer_C"),
		TEXT("/Game/Blueprints/Weapon/BP_Rifle.BP_Rifle_C")
	};
	if (!TestNotNull(TEXT("Concrete Shooter BP"), ShooterClass)) return false;

	const FBoolProperty* FPInAir = FindFProperty<FBoolProperty>(UShooterFirstPersonAnimInstance::StaticClass(), TEXT("bIsInAir"));
	const FBoolProperty* FPFalling = FindFProperty<FBoolProperty>(UShooterFirstPersonAnimInstance::StaticClass(), TEXT("bIsFalling"));
	const FBoolProperty* FPGrounded = FindFProperty<FBoolProperty>(UShooterFirstPersonAnimInstance::StaticClass(), TEXT("bIsGrounded"));
	const FFloatProperty* FPJumpAlpha = FindFProperty<FFloatProperty>(UShooterFirstPersonAnimInstance::StaticClass(), TEXT("ViewModelJumpLandAlpha"));
	const FBoolProperty* TPInAir = FindFProperty<FBoolProperty>(UShooterAnimInstance::StaticClass(), TEXT("bIsInAir"));
	const FBoolProperty* TPGrounded = FindFProperty<FBoolProperty>(UShooterAnimInstance::StaticClass(), TEXT("bIsGrounded"));
	if (!TestTrue(TEXT("Landing properties remain available to AnimBlueprints"),
		FPInAir && FPFalling && FPGrounded && FPJumpAlpha && TPInAir && TPGrounded)) return false;

	const FName WorldName = MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("ShooterLanding"),
		EUniqueObjectNameOptions::GloballyUnique);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Landing animation test world"), World))
	{
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->AddToRoot();
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(ShooterClass, Spawn);
	if (!TestNotNull(TEXT("Landing animation Shooter"), Shooter)) return false;
	UCharacterMovementComponent* Movement = Shooter->GetCharacterMovement();
	if (!TestNotNull(TEXT("Landing movement component"), Movement)) return false;

	// BP에 저장된 실제 PreSuit/Suit Mesh·ABP를 적용한다. 같은 Skeleton으로 대체하면 전환별 회귀를 놓친다.
	for (bool bSuit : {false, true})
	{
		const FStructProperty* ConfigurationProperty = FindFProperty<FStructProperty>(ShooterClass,
			bSuit ? TEXT("SuitPresentation") : TEXT("PreSuitPresentation"));
		if (!TestNotNull(TEXT("Configured presentation property"), ConfigurationProperty)) return false;
		const auto* Configuration = ConfigurationProperty->ContainerPtrToValuePtr<FShooterPresentationConfiguration>(Shooter);
		FString Error;
		if (!TestTrue(TEXT("Actual BP presentation is complete"), Configuration->Validate(Error)))
		{
			AddError(Error);
			return false;
		}
		if (!TestTrue(TEXT("Configured presentation applies"), Shooter->SetSuitPresentation(bSuit))) return false;
		auto* FP = Cast<UShooterFirstPersonAnimInstance>(Shooter->GetFirstPersonMesh()->GetAnimInstance());
		auto* TP = Cast<UShooterAnimInstance>(Shooter->GetMesh()->GetAnimInstance());
		if (!TestNotNull(TEXT("Configured FP animation instance"), FP)
			|| !TestNotNull(TEXT("Configured TP animation instance"), TP)) return false;
		USkeletalMeshComponent* FPMesh = Shooter->GetFirstPersonMesh();
		USkeletalMeshComponent* TPMesh = Shooter->GetMesh();
		for (USkeletalMeshComponent* Mesh : {FPMesh, TPMesh})
		{
			// 렌더링하지 않는 테스트 월드에서도 갱신 생략 없이 실제 그래프를 평가한다.
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bEnableUpdateRateOptimizations = false;
		}

		for (const TCHAR* WeaponPath : WeaponPaths)
		{
			UClass* WeaponClass = LoadClass<AWeaponBase>(nullptr, WeaponPath);
			if (!TestNotNull(TEXT("Concrete landing weapon BP"), WeaponClass)) return false;
			const AWeaponBase* WeaponDefaults = CastChecked<AWeaponBase>(WeaponClass->GetDefaultObject());
			if (!bSuit && WeaponDefaults->GetWeaponType() == EWeaponType::Rifle) continue;
			AWeaponBase* Weapon = World->SpawnActor<AWeaponBase>(WeaponClass, Spawn);
			if (!TestNotNull(TEXT("Landing weapon"), Weapon)) return false;
			Shooter->AFirstPersonCharacter::EquipWeapon(Weapon);
			Shooter->RefreshWeaponMode();

			for (int32 FramesPerSecond : {30, 60, 120})
			{
				const float Delta = 1.0f / FramesPerSecond;
				for (int32 Case = 0; Case < 4; ++Case)
				{
					const TCHAR* Name = Case == 0 ? TEXT("ShortJumpStart") : Case == 1 ? TEXT("OrdinaryJump")
						: Case == 2 ? TEXT("DoubleJump") : TEXT("Drop");
					const FString Label = FString::Printf(TEXT("%s/%s/%dFPS/%s"), bSuit ? TEXT("Suit") : TEXT("PreSuit"),
						*WeaponClass->GetName(), FramesPerSecond, Name);
					ShooterLandingAnimationTests::FGraphProbe* FPProbe = nullptr;
					ShooterLandingAnimationTests::FGraphProbe* TPProbe = nullptr;
					const bool bDetailedTrace = FramesPerSecond == 60 && Case == 1;
					const auto UpdateAnimation = [this, FPMesh, TPMesh, Delta, &Label, &FPProbe, &TPProbe](bool bTrace = false)
					{
						// linked graph의 프레임 중복 갱신 방지도 실제 프레임처럼 전진시킨다.
						++GFrameCounter;
						// NativeUpdate만 호출하면 전이 규칙이 실행되지 않는다. Mesh 경유로 갱신하고,
						// TickFunction 없는 동기 평가를 끝낸 뒤 상태와 블렌드를 읽는다.
						for (USkeletalMeshComponent* Mesh : {FPMesh, TPMesh})
						{
							const auto* Probe = Mesh == FPMesh ? FPProbe : TPProbe;
							const auto LogStage = [this, bTrace, Probe, Mesh, &Label](const TCHAR* Stage)
							{
								if (!bTrace || !Probe) return;
								AddInfo(FString::Printf(TEXT("[LandingEvaluationTest] %s Mesh=%s Stage=%s Frame=%llu %s Graph=%s Timing=%s"),
									*Label, *Mesh->GetName(), Stage, static_cast<unsigned long long>(GFrameCounter),
									*Probe->DescribeEvaluation(Mesh), *Probe->Describe(), *Probe->DescribeLandingTiming()));
							};
							LogStage(TEXT("BeforeTick"));
							// 장비/근접 정리가 서버용 montage-only 정책을 다시 적용할 수 있다.
							// 렌더링 없는 그래프 테스트에서는 매 Tick 직전에 포즈 갱신을 보장한다.
							if (Mesh->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
							{
								AddInfo(FString::Printf(TEXT("[LandingFixtureTest] %s Mesh=%s RestoreVisibilityTick=%d->%d"),
									*Label, *Mesh->GetName(), static_cast<int32>(Mesh->VisibilityBasedAnimTickOption),
									static_cast<int32>(EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)));
								Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
							}
							UAnimInstance* Animation = Mesh->GetAnimInstance();
							const int32 PreviousCounter = Animation->GetUpdateCounter().Get();
							Mesh->TickAnimation(Delta, false);
							// 병렬 갱신이 예약된 경우 AfterTick은 완료 전일 수 있다. AfterRefresh와 비교해 해석한다.
							LogStage(TEXT("AfterTick"));
							Mesh->RefreshBoneTransforms();
							LogStage(TEXT("AfterRefresh"));
							// 0초 강제 평가만 실행된 프레임을 착지 전이 실패로 오인하지 않도록 fixture 자체도 검증한다.
							TestTrue(*(Label + TEXT(" ") + Mesh->GetName() + TEXT(" graph update counter advances")),
								Animation->GetUpdateCounter().Get() != PreviousCounter);
							TestTrue(*(Label + TEXT(" ") + Mesh->GetName() + TEXT(" graph receives the requested frame delta")),
								FMath::IsNearlyEqual(Animation->GetDeltaSeconds(), Delta, SMALL_NUMBER));
						}
					};

					Movement->SetMovementMode(MOVE_Walking);
					Movement->Velocity = FVector::ZeroVector;
					FPJumpAlpha->SetPropertyValue_InContainer(FP, 0.0f);
					UpdateAnimation();
					// 무기 포즈와 이전 케이스의 착지 블렌드를 안정시킨 뒤 새 점프를 시작한다.
					for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame) UpdateAnimation();
					ShooterLandingAnimationTests::FGraphProbe FPGraph(FP);
					ShooterLandingAnimationTests::FGraphProbe TPGraph(TP);
					FPProbe = &FPGraph;
					TPProbe = &TPGraph;
					if (!TestTrue(*(Label + TEXT(" FP graph evaluates an active state machine")), FPGraph.HasActiveMachine())
						|| !TestTrue(*(Label + TEXT(" TP graph evaluates an active state machine")), TPGraph.HasActiveMachine())
						|| !TestTrue(*(Label + TEXT(" TP graph contains the authored jump states")), TPGraph.bHasAirStates)) return false;
					Movement->SetMovementMode(MOVE_Falling);
					Movement->Velocity = FVector(100.0f, 0.0f, Case == 3 ? -300.0f : 300.0f);
					const int32 AirFrames = Case == 0 ? 1 : FramesPerSecond / 2;
					for (int32 Frame = 0; Frame < AirFrames; ++Frame)
					{
						if (Case == 2 && Frame == AirFrames / 2) Movement->Velocity.Z = 300.0f;
						else if (Case != 0 && Frame >= AirFrames / 2) Movement->Velocity.Z = -300.0f;
						UpdateAnimation();
						FPGraph.ObserveAir();
						TPGraph.ObserveAir();
					}
					TestTrue(*(Label + TEXT(" TP graph actually enters a jump state")), TPGraph.ObservedAirMachines.Num() > 0);
					if (FPGraph.bHasAirStates)
					{
						TestTrue(*(Label + TEXT(" FP graph actually enters its authored jump state")), FPGraph.ObservedAirMachines.Num() > 0);
					}
					const FString AirGraph = TPGraph.Describe();
					TestTrue(*(Label + TEXT(" FP and TP enter air state")), FPInAir->GetPropertyValue_InContainer(FP)
						&& FPFalling->GetPropertyValue_InContainer(FP) && TPInAir->GetPropertyValue_InContainer(TP));
					const float AirAlpha = FPJumpAlpha->GetPropertyValue_InContainer(FP);
					if (WeaponPath == WeaponPaths[0] && FramesPerSecond == 30 && Case == 0)
					{
						for (const FString& Node : TPGraph.DescribePoseLinks())
						{
							AddInfo(FString::Printf(TEXT("[LandingTopologyTest] %s %s"), bSuit ? TEXT("Suit/TP") : TEXT("PreSuit/TP"), *Node));
						}
					}
					// 공중 마지막 프레임을 기준점으로 삼아 착지 첫 프레임의 정지/시간 역행도 집계한다.
					FPGraph.SampleGroundedFrame(FPMesh);
					TPGraph.SampleGroundedFrame(TPMesh);

					// 물리의 착지 확정 경계만 입력한다. 점프 횟수/충돌 판정은 이번 테스트 범위가 아니다.
					// 공중 상태는 다음 그래프 갱신에 끝나되 JumpEnd와 이전 포즈의 블렌드는 별도로 측정한다.
					Movement->SetMovementMode(MOVE_Walking);
					Movement->Velocity = FVector(100.0f, 0.0f, 0.0f);
					UpdateAnimation(bDetailedTrace);
					FPGraph.SampleGroundedFrame(FPMesh);
					TPGraph.SampleGroundedFrame(TPMesh);
					const FString LandingGraph = TPGraph.Describe();
					const auto LogLandingTiming = [this, &Label, &FPGraph, &TPGraph, FPMesh, TPMesh](const TCHAR* Phase, float GroundedElapsed)
					{
						AddInfo(FString::Printf(TEXT("[LandingTimingTest] %s Phase=%s GroundedElapsed=%.6f TP=%s FP=%s"),
							*Label, Phase, GroundedElapsed, *TPGraph.DescribeLandingTiming(), *FPGraph.DescribeLandingTiming()));
						AddInfo(FString::Printf(TEXT("[LandingContextTest] %s Phase=%s TP=%s FP=%s"),
							*Label, Phase, *TPGraph.DescribeEvaluation(TPMesh), *FPGraph.DescribeEvaluation(FPMesh)));
					};
					LogLandingTiming(TEXT("FirstGrounded"), Delta);
					TestFalse(*(Label + TEXT(" TP graph exits jump states on the first grounded update: ") + LandingGraph), TPGraph.HasCurrentAirState());
					TestFalse(*(Label + TEXT(" FP graph exits any authored jump states: ") + FPGraph.Describe()), FPGraph.HasCurrentAirState());
					TestFalse(*(Label + TEXT(" next FP update leaves air immediately")),
						FPInAir->GetPropertyValue_InContainer(FP) || FPFalling->GetPropertyValue_InContainer(FP));
					TestFalse(*(Label + TEXT(" next TP update leaves air immediately")), TPInAir->GetPropertyValue_InContainer(TP));
					TestTrue(*(Label + TEXT(" both animations are grounded")),
						FPGrounded->GetPropertyValue_InContainer(FP) && TPGrounded->GetPropertyValue_InContainer(TP));
					float PreviousAlpha = FPJumpAlpha->GetPropertyValue_InContainer(FP);
					TestTrue(*(Label + TEXT(" landing alpha starts blending without being snapped to zero")),
						PreviousAlpha > 0.0f && PreviousAlpha < AirAlpha);
					for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
					{
						UpdateAnimation(bDetailedTrace && Frame < 2);
						FPGraph.SampleGroundedFrame(FPMesh);
						TPGraph.SampleGroundedFrame(TPMesh);
						if (Frame + 1 == FramesPerSecond / 2)
						{
							LogLandingTiming(TEXT("HalfSecond"), Delta * (Frame + 2));
						}
						TestFalse(*(Label + TEXT(" grounded TP graph cannot reenter a jump state")), TPGraph.HasCurrentAirState());
						TestFalse(*(Label + TEXT(" grounded FP graph cannot reenter a jump state")), FPGraph.HasCurrentAirState());
						const float Alpha = FPJumpAlpha->GetPropertyValue_InContainer(FP);
						TestTrue(*(Label + TEXT(" landing alpha never rebounds")), Alpha >= 0.0f && Alpha <= PreviousAlpha);
						PreviousAlpha = Alpha;
					}
					LogLandingTiming(TEXT("Settled"), Delta * (FramesPerSecond + 1));
					AddInfo(FString::Printf(TEXT("[LandingFrameSummary] %s TP={%s} FP={%s}"),
						*Label, *TPGraph.DescribeFrameSummary(), *FPGraph.DescribeFrameSummary()));
					TestTrue(*(Label + TEXT(" landing alpha settles under existing interpolation")), PreviousAlpha < 0.001f);
					TestTrue(*(Label + TEXT(" TP jump pose blend clears within one second")), TPGraph.GetAirWeight() < 0.001f);
					TestTrue(*(Label + TEXT(" FP jump pose blend clears within one second")), FPGraph.GetAirWeight() < 0.001f);
					TestFalse(*(Label + TEXT(" TP landing state finishes within one second: ") + TPGraph.Describe()), TPGraph.HasLandingState());
					TestFalse(*(Label + TEXT(" FP landing state finishes within one second: ") + FPGraph.Describe()), FPGraph.HasLandingState());
					AddInfo(FString::Printf(TEXT("[LandingGraphTest] %s Air=%s FirstGrounded=%s Settled=%s AirWeight=%.6f FP=%s FPJumpStates=%d"),
						*Label, *AirGraph, *LandingGraph, *TPGraph.Describe(), TPGraph.GetAirWeight(),
						*FPGraph.Describe(), FPGraph.bHasAirStates ? 1 : 0));
					AddInfo(FString::Printf(TEXT("[LandingStateTest] %s FP=%s TP=%s AirAlpha=%.6f SettledAlpha=%.6f"),
						*Label, *FP->GetClass()->GetPathName(), *TP->GetClass()->GetPathName(), AirAlpha, PreviousAlpha));
				}
			}
		}
	}
	return true;
}

#endif
