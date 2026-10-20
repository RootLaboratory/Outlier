#include "OutlierShooterSuitTestToolWidget.h"

#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/SuitInteraction.h"
#include "Interaction/Level1SuitUpgradeDoor.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OutlierShooterSuitTester"

DEFINE_LOG_CATEGORY_STATIC(LogShooterSuitTester, Log, All);

TArray<FName> OutlierShooterSuitTest::ToggleNodes(const TArray<FName>& Current, const TArray<FName>& Matching, bool bEnabled)
{
	TArray<FName> Result = Current;
	if (!bEnabled)
	{
		Result.RemoveAll([&Matching](FName Node) { return Matching.Contains(Node); });
	}
	else if (!Matching.IsEmpty() && !Matching.ContainsByPredicate([&Current](FName Node) { return Current.Contains(Node); }))
	{
		// 같은 태그를 주는 노드가 여러 개여도 하나만 추가해 수치 효과의 중복 적용을 피한다.
		Result.AddUnique(Matching[0]);
	}
	return Result;
}

TArray<FName> OutlierShooterSuitTest::RestoreNodes(const TArray<FName>& Current, const TArray<FName>& Original, const TArray<FName>& Controlled)
{
	TArray<FName> Result = ToggleNodes(Current, Controlled, false);
	for (FName Node : Original)
	{
		if (Controlled.Contains(Node))
		{
			Result.AddUnique(Node);
		}
	}
	return Result;
}

void SOutlierShooterSuitTestToolWidget::Construct(const FArguments& InArgs)
{
	Features =
	{
		{OutlierGameplayTags::Ability::Shooter::Stealth(), LOCTEXT("Stealth", "은신 사용 가능"), LOCTEXT("StealthTip", "은신 해금 노드를 켜고 끕니다. 스킬을 즉시 발동시키지는 않습니다."), {}},
		{OutlierGameplayTags::Ability::Shooter::WeaponOvercharge(), LOCTEXT("Overcharge", "과부하 사용 가능"), LOCTEXT("OverchargeTip", "과부하 해금 노드를 켜고 끕니다. 사용하려면 소총과 Partner 조건도 충족해야 합니다."), {}},
		{OutlierGameplayTags::Ability::Shooter::BulletReflection(), LOCTEXT("Reflection", "반사 사용 가능"), LOCTEXT("ReflectionTip", "반사 해금 노드를 켜고 끕니다."), {}},
		{OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::InvincibleAfterLeap(), LOCTEXT("LeapImmunity", "양자 이동 성공 후 무적"), LOCTEXT("LeapImmunityTip", "무적 도약 업그레이드입니다. 이동 성공 시 DT에 설정된 시간 동안 무적이 됩니다."), {}},
		{OutlierGameplayTags::Upgrade::Shooter::QuantumLeap::DoubleCharge(), LOCTEXT("DoubleLeap", "양자 이동 최대 2회"), LOCTEXT("DoubleLeapTip", "도약 부스트 노드를 켜고 끕니다. 기존 순차 충전 규칙을 사용합니다."), {}},
		{OutlierGameplayTags::Upgrade::Shooter::WeaponOvercharge::AutoAim(), LOCTEXT("AutoAim", "과부하 오토 에임"), LOCTEXT("AutoAimTip", "오토 에임 업그레이드입니다. 과부하를 실제로 사용 중일 때 적용됩니다."), {}}
	};
	FEditorDelegates::PostPIEStarted.AddSP(this, &SOutlierShooterSuitTestToolWidget::HandlePIEChanged);
	FEditorDelegates::ShutdownPIE.AddSP(this, &SOutlierShooterSuitTestToolWidget::HandlePIEChanged);

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SAssignNew(TargetCombo, SComboBox<FTargetPtr>)
			.OptionsSource(&Targets)
			.OnComboBoxOpening(this, &SOutlierShooterSuitTestToolWidget::RefreshTargets)
			.OnSelectionChanged(this, &SOutlierShooterSuitTestToolWidget::SelectTarget)
			.OnGenerateWidget_Lambda([](FTargetPtr Target) { return SNew(STextBlock).Text(Target->Label); })
			[
				SNew(STextBlock).Text_Lambda([this]() { return SelectedTarget.IsValid() ? SelectedTarget->Label : LOCTEXT("NoTarget", "PIE Shooter 없음"); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("Refresh", "대상 새로고침"))
			.OnClicked_Lambda([this]() { RefreshTargets(); return FReply::Handled(); })
		]
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(STextBlock).AutoWrapText(true)
		.Text(LOCTEXT("Help", "PIE 서버의 실제 슈트/업그레이드 상태를 변경합니다. 첫 변경부터 은신·과부하·반사는 체크된 노드만 해금됩니다(NoGrantMode 우회 해제). 비용·선행 노드는 생략하며 거리·쿨타임·무기 조건은 유지합니다. 별도 프로세스 서버는 지원하지 않습니다."))
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SCheckBox).IsEnabled_Lambda([this]() { return CanEdit(); })
		.IsChecked_Lambda([this]() { return CanEdit() && SelectedTarget->PlayerState->GetAcquiredSuit() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged(this, &SOutlierShooterSuitTestToolWidget::SetSuitState)
		.ToolTipText(LOCTEXT("SuitTip", "적용 시 Shooter 슈트 메시와 소총을 함께 지급합니다. 소총 설정은 현재 장비 기록 또는 맵의 SuitInteraction에서 읽습니다. 해제 시 도구가 추가한 소총만 회수합니다."))
		[SNew(STextBlock).Text(LOCTEXT("Suit", "슈트 획득"))]
	];
	for (int32 Index = 0; Index < Features.Num(); ++Index)
	{
		Rows->AddSlot().AutoHeight().Padding(6.0f)
		[
			SNew(SCheckBox)
			.IsEnabled_Lambda([this, Index]() { return CanEdit() && !Features[Index].Nodes.IsEmpty(); })
			.IsChecked_Lambda([this, Index]() { return GetFeatureState(Index); })
			.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State) { SetFeatureState(Index, State); })
			.ToolTipText_Lambda([this, Index]()
			{
				return Features[Index].Nodes.IsEmpty() ? LOCTEXT("MissingNode", "해당 태그를 주는 업그레이드 노드가 없습니다. 대상 새로고침과 Upgrade DT 설정을 확인하세요.") : Features[Index].Tooltip;
			})
			[SNew(STextBlock).Text(Features[Index].Label)]
		];
	}
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SNumericEntryBox<float>)
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.MinValue(1.0f).MaxValue(100000.0f)
			.Value_Lambda([this]() -> TOptional<float> { return TestDamageAmount; })
			.OnValueChanged_Lambda([this](float Value) { TestDamageAmount = FMath::Clamp(Value, 1.0f, 100000.0f); })
			.Label()[SNew(STextBlock).Text(LOCTEXT("DamageAmount", "피해량"))]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("DamageShooter", "Shooter 피해 적용"))
			.ToolTipText(LOCTEXT("DamageShooterTip", "환경 피해를 적용합니다. 무적·실드·피격에 의한 은신 해제를 확인할 수 있습니다."))
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.OnClicked(this, &SOutlierShooterSuitTestToolWidget::DamageShooter)
		]
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SButton).Text(LOCTEXT("StartLevel1Combat", "Level1 문 연결 전투 시작"))
		.ToolTipText(LOCTEXT("StartLevel1CombatTip", "가장 가까운 Level1 문의 CombatRoom 전투를 시작합니다. 입장·업그레이드 UI·문 연출은 건너뛰며 첫 Wave는 기존처럼 3초 뒤 소환됩니다. 전투 진행은 복원 버튼으로 되돌리지 않습니다."))
		.IsEnabled_Lambda([this]() { return CanEdit(); })
		.OnClicked(this, &SOutlierShooterSuitTestToolWidget::StartLevel1Combat)
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SButton).Text(LOCTEXT("Restore", "테스트 전 상태로 복원"))
		.IsEnabled_Lambda([this]() { return CanEdit() && OriginalStates.Contains(SelectedTarget->PlayerState); })
		.OnClicked(this, &SOutlierShooterSuitTestToolWidget::RestoreOriginalState)
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Status; })];
	ChildSlot[SNew(SScrollBox) + SScrollBox::Slot()[Rows]];
	RefreshTargets();
}

SOutlierShooterSuitTestToolWidget::~SOutlierShooterSuitTestToolWidget()
{
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::ShutdownPIE.RemoveAll(this);
}

void SOutlierShooterSuitTestToolWidget::HandlePIEChanged(bool bSimulating)
{
	OriginalStates.Reset();
	RefreshTargets();
}

void SOutlierShooterSuitTestToolWidget::RefreshTargets()
{
	const TWeakObjectPtr<AShooterCharacter> Previous = SelectedTarget.IsValid() ? SelectedTarget->Shooter : nullptr;
	Targets.Reset();
	SelectedTarget.Reset();
	if (GEngine)
	{
		// 검색은 대상 갱신 시에만 한다. 체크 상태를 그릴 때는 캐시된 약한 참조만 읽는다.
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType != EWorldType::PIE || !World || World->GetNetMode() == NM_Client)
			{
				continue;
			}
			for (TActorIterator<AShooterCharacter> It(World); It; ++It)
			{
				AShooterCharacter* Shooter = *It;
				AOutlierPlayerState* PS = Shooter->GetPlayerState<AOutlierPlayerState>();
				UOutlierAbilitySystemComponent* ASC = Cast<UOutlierAbilitySystemComponent>(Shooter->GetAbilitySystemComponent());
				if (!Shooter->HasAuthority() || !PS || !ASC || !ASC->IsShooterSuitConfigured() || !Shooter->GetUpgradeComponent())
				{
					continue;
				}
				FTargetPtr Target = MakeShared<FTarget>();
				Target->Shooter = Shooter;
				Target->PlayerState = PS;
				Target->ASC = ASC;
				Target->Upgrades = Shooter->GetUpgradeComponent();
				// PlayerState가 살아 있으면 Pawn 재생성 뒤에도 테스트 모드와 복원 지점을 이어간다.
				if (OriginalStates.Contains(Target->PlayerState) && !ASC->AreEditorShooterSuitGrantChecksEnabled())
				{
					ASC->SetEditorShooterSuitGrantChecks(true);
				}
				Target->Label = FText::FromString(FString::Printf(TEXT("%s / %s / %s"), *World->GetName(), *PS->GetPlayerName(), *Shooter->GetName()));
				Targets.Add(Target);
				if (Target->Shooter == Previous)
				{
					SelectedTarget = Target;
				}
			}
		}
	}
	if (!SelectedTarget.IsValid() && !Targets.IsEmpty())
	{
		SelectedTarget = Targets[0];
	}
	if (TargetCombo)
	{
		TargetCombo->RefreshOptions();
		TargetCombo->SetSelectedItem(SelectedTarget);
	}
	RefreshFeatureNodes();
	Status = Targets.IsEmpty() ? LOCTEXT("Waiting", "PIE에서 Shooter 생성 후 대상 새로고침을 누르세요. Run Under One Process가 필요합니다.")
		: LOCTEXT("Ready", "체크 변경 시 즉시 적용합니다. 첫 변경 시점의 상태를 복원할 수 있습니다.");
}

void SOutlierShooterSuitTestToolWidget::SelectTarget(FTargetPtr Target, ESelectInfo::Type SelectInfo)
{
	SelectedTarget = Target;
	RefreshFeatureNodes();
}

bool SOutlierShooterSuitTestToolWidget::CanEdit() const
{
	return SelectedTarget.IsValid() && SelectedTarget->Shooter.IsValid() && SelectedTarget->PlayerState.IsValid()
		&& SelectedTarget->ASC.IsValid() && SelectedTarget->Upgrades.IsValid()
		&& SelectedTarget->Shooter->GetPlayerState<AOutlierPlayerState>() == SelectedTarget->PlayerState.Get()
		&& SelectedTarget->Shooter->HasAuthority() && SelectedTarget->Shooter->GetWorld()
		&& SelectedTarget->Shooter->GetWorld()->WorldType == EWorldType::PIE;
}

void SOutlierShooterSuitTestToolWidget::RefreshFeatureNodes()
{
	for (FFeature& Feature : Features)
	{
		Feature.Nodes.Reset();
	}
	if (!CanEdit())
	{
		return;
	}
	TArray<FName> RowNames;
	SelectedTarget->Upgrades->GetNodeRowNames(RowNames);
	RowNames.Sort(FNameLexicalLess());
	for (FName RowName : RowNames)
	{
		for (const FOutlierUpgradeEffectRow& Effect : SelectedTarget->Upgrades->GetNodeEffects(RowName))
		{
			if (Effect.EffectType != EOutlierUpgradeEffectType::GrantAbility && Effect.EffectType != EOutlierUpgradeEffectType::FunctionOverride)
			{
				continue;
			}
			for (FFeature& Feature : Features)
			{
				if (Feature.Tag == Effect.TargetTag)
				{
					Feature.Nodes.AddUnique(RowName);
				}
			}
		}
	}
}

ECheckBoxState SOutlierShooterSuitTestToolWidget::GetFeatureState(int32 Index) const
{
	return CanEdit() && SelectedTarget->Upgrades->HasUpgradeTag(Features[Index].Tag) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOutlierShooterSuitTestToolWidget::CaptureOriginalState()
{
	if (!OriginalStates.Contains(SelectedTarget->PlayerState))
	{
		FOriginalState Original;
		Original.Nodes = SelectedTarget->PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter);
		Original.bSuit = SelectedTarget->PlayerState->GetAcquiredSuit();
		Original.bGrantChecks = SelectedTarget->ASC->AreEditorShooterSuitGrantChecksEnabled();
		Original.FirstPersonSuitMesh = SelectedTarget->PlayerState->GetSuitFirstPersonMesh();
		Original.ThirdPersonSuitMesh = SelectedTarget->PlayerState->GetSuitThirdPersonMesh();
		OriginalStates.Add(SelectedTarget->PlayerState, MoveTemp(Original));
	}
	FOriginalState& Original = OriginalStates.FindChecked(SelectedTarget->PlayerState);
	// DT를 갱신한 뒤 새 노드를 시험해도 이번 도구가 건드린 범위는 복원 대상에 포함한다.
	for (const FFeature& Feature : Features)
	{
		for (FName Node : Feature.Nodes)
		{
			Original.ControlledNodes.AddUnique(Node);
		}
	}
}

void SOutlierShooterSuitTestToolWidget::ApplyNodes(const TArray<FName>& Nodes) const
{
	// PS 원본을 바꾸면 기존 통보가 ASC 투영과 소유 클라이언트 HUD 갱신까지 수행한다.
	SelectedTarget->PlayerState->RestoreCheckpointProgress(SelectedTarget->PlayerState->GetNodeCount(), EOutlierUpgradeRole::Shooter, Nodes);
}

void SOutlierShooterSuitTestToolWidget::SetFeatureState(int32 Index, ECheckBoxState State)
{
	if (!CanEdit() || Features[Index].Nodes.IsEmpty())
	{
		return;
	}
	CaptureOriginalState();
	SelectedTarget->ASC->SetEditorShooterSuitGrantChecks(true);
	const bool bEnabled = State == ECheckBoxState::Checked;
	if (!bEnabled)
	{
		const FGameplayTag FeatureTag = Features[Index].Tag;
		if (FeatureTag == OutlierGameplayTags::Ability::Shooter::Stealth())
		{
			SelectedTarget->ASC->EndActiveShooterStealth(false);
		}
		if (FeatureTag == OutlierGameplayTags::Ability::Shooter::BulletReflection())
		{
			SelectedTarget->ASC->EndActiveShooterBulletReflection(false);
		}
		if (FeatureTag == OutlierGameplayTags::Ability::Shooter::WeaponOvercharge())
		{
			SelectedTarget->ASC->EndActiveShooterWeaponOvercharge(false);
		}
	}
	ApplyNodes(OutlierShooterSuitTest::ToggleNodes(SelectedTarget->PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter), Features[Index].Nodes, bEnabled));
	Status = FText::Format(LOCTEXT("Applied", "{0}: {1} — 해금은 실제 노드 기준입니다. 체크 해제한 능력은 NoGrantMode로 허용되지 않습니다."),
		Features[Index].Label, bEnabled ? LOCTEXT("On", "활성") : LOCTEXT("Off", "비활성"));
}

void SOutlierShooterSuitTestToolWidget::StopAbilities() const
{
	SelectedTarget->ASC->CancelActiveShooterQuantumLeap(false);
	SelectedTarget->ASC->EndActiveShooterStealth(false);
	SelectedTarget->ASC->EndActiveShooterBulletReflection(false);
	SelectedTarget->ASC->EndActiveShooterWeaponOvercharge(false);
}

void SOutlierShooterSuitTestToolWidget::SetSuitState(ECheckBoxState State)
{
	if (!CanEdit())
	{
		return;
	}
	CaptureOriginalState();
	if (State == ECheckBoxState::Checked && !GrantSuitEquipment())
	{
		return;
	}
	if (State != ECheckBoxState::Checked)
	{
		StopAbilities();
		RemoveGrantedRifle(OriginalStates.FindChecked(SelectedTarget->PlayerState));
	}
	SelectedTarget->ASC->SetEditorShooterSuitGrantChecks(true);
	SelectedTarget->PlayerState->SetAcquiredSuit(State == ECheckBoxState::Checked);
	Status = LOCTEXT("SuitApplied", "슈트 상태와 외형을 적용했습니다. 획득 시 소총을 지급·장착하며, 해제 시 도구가 추가한 소총만 회수합니다.");
}

bool SOutlierShooterSuitTestToolWidget::GrantSuitEquipment()
{
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	if (!Inventory)
	{
		Status = LOCTEXT("NoInventory", "Shooter 인벤토리가 없어 슈트를 지급하지 못했습니다.");
		return false;
	}

	// 소비된 PIE 슈트가 없으면 에디터 맵의 원본 설정을 읽는다. 상호작용 액터 자체는 소비하지 않는다.
	const ASuitInteraction* SuitSource = nullptr;
	auto FindSuitSource = [&SuitSource](UWorld* World)
	{
		if (!World) return;
		for (TActorIterator<ASuitInteraction> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetEditorShooterRifleClass())
			{
				SuitSource = *It;
				break;
			}
		}
	};
	FindSuitSource(Shooter->GetWorld());
	if (!SuitSource && GEditor) FindSuitSource(GEditor->GetEditorWorldContext().World());

	AWeaponBase* Rifle = Inventory->GetWeaponInSlot(EWeaponSlot::Primary);
	if (Rifle && Rifle->GetWeaponType() != EWeaponType::Rifle)
	{
		Status = LOCTEXT("OccupiedPrimary", "Primary 슬롯에 소총이 아닌 무기가 있어 지급을 중단했습니다.");
		return false;
	}
	TSubclassOf<AWeaponBase> RifleClass;
	const FOutlierLoadoutSnapshot& SavedLoadout = SelectedTarget->PlayerState->GetLoadoutSnapshot();
	const int32 PrimaryIndex = static_cast<int32>(EWeaponSlot::Primary);
	if (SavedLoadout.SlotSnapshots.IsValidIndex(PrimaryIndex)) RifleClass = SavedLoadout.SlotSnapshots[PrimaryIndex].WeaponClass;
	if (!RifleClass && SuitSource) RifleClass = SuitSource->GetEditorShooterRifleClass();
	if (!Rifle && (!RifleClass || RifleClass->GetDefaultObject<AWeaponBase>()->GetWeaponType() != EWeaponType::Rifle))
	{
		Status = LOCTEXT("NoRifleConfiguration", "소총 설정을 찾지 못했습니다. 맵에 ShooterRifleClass가 설정된 SuitInteraction이 필요합니다.");
		return false;
	}

	USkeletalMesh* FirstPersonMesh = SuitSource ? SuitSource->GetEditorShooterFirstPersonMesh() : SelectedTarget->PlayerState->GetSuitFirstPersonMesh();
	USkeletalMesh* ThirdPersonMesh = SuitSource ? SuitSource->GetEditorShooterThirdPersonMesh() : SelectedTarget->PlayerState->GetSuitThirdPersonMesh();
	if (!Shooter->ApplySuitMeshes(FirstPersonMesh, ThirdPersonMesh))
	{
		Status = LOCTEXT("InvalidSuitMeshes", "슈트 외형 적용에 실패했습니다. Shooter BP의 SuitPresentation 메시·ABP 설정을 확인하세요.");
		return false;
	}

	FOriginalState& Original = OriginalStates.FindChecked(SelectedTarget->PlayerState);
	if (!Rifle)
	{
		Original.BeforeRifleGrant = SelectedTarget->PlayerState->GetLoadoutSnapshot();
		Inventory->BuildLoadoutSnapshot(Original.BeforeRifleGrant, true);
		Rifle = AWeaponBase::SpawnLoadoutWeapon(Shooter->GetWorld(), RifleClass, Shooter);
		if (!Rifle || !Inventory->EquipSuitRifle(Rifle))
		{
			if (Rifle) Rifle->Destroy();
			Shooter->RefreshPresentationFromPlayerState();
			Status = LOCTEXT("RifleGrantFailed", "소총 생성·장착에 실패했습니다. 슈트 획득 상태는 변경하지 않았습니다.");
			return false;
		}
		Original.GrantedRifle = Rifle;
	}
	else
	{
		Inventory->SelectWeaponSlot(EWeaponSlot::Primary);
	}
	// PS의 획득 상태가 외형을 다시 투영할 때도 실제 적용한 메시를 사용하도록 맞춘다.
	SelectedTarget->PlayerState->SetSuitMeshes(Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset(), Shooter->GetMesh()->GetSkeletalMeshAsset());
	return true;
}

void SOutlierShooterSuitTestToolWidget::RemoveGrantedRifle(FOriginalState& Original)
{
	UShooterInventoryComponent* Inventory = SelectedTarget->Shooter->GetInventoryComponent();
	if (Inventory && Original.GrantedRifle.IsValid() && Inventory->GetWeaponInSlot(EWeaponSlot::Primary) == Original.GrantedRifle.Get())
	{
		// 현재 탄약과 다른 슬롯은 보존하고 도구가 채웠던 Primary만 지급 전 상태로 복원한다.
		FOutlierLoadoutSnapshot Snapshot = SelectedTarget->PlayerState->GetLoadoutSnapshot();
		Inventory->BuildLoadoutSnapshot(Snapshot, true);
		const int32 PrimaryIndex = static_cast<int32>(EWeaponSlot::Primary);
		if (!Snapshot.SlotSnapshots.IsValidIndex(PrimaryIndex))
		{
			return;
		}
		Snapshot.SlotSnapshots[PrimaryIndex] = Original.BeforeRifleGrant.SlotSnapshots.IsValidIndex(PrimaryIndex)
			? Original.BeforeRifleGrant.SlotSnapshots[PrimaryIndex] : FOutlierWeaponSnapshot{};
		if (Snapshot.CurrentSlot == EWeaponSlot::Primary)
		{
			Snapshot.CurrentSlot = Original.BeforeRifleGrant.CurrentSlot;
		}
		Inventory->RestoreLoadout(Snapshot, true);
	}
	Original.GrantedRifle.Reset();
}

FReply SOutlierShooterSuitTestToolWidget::DamageShooter()
{
	if (!CanEdit()) return FReply::Handled();
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	const UAbilitySystemComponent* ASC = Shooter->GetAbilitySystemComponent();
	const bool bDamageImmune = ASC && ASC->HasMatchingGameplayTag(OutlierGameplayTags::State::DamageImmune());
	const float HealthBefore = Shooter->GetCurHealth();
	const float ShieldBefore = Shooter->GetCurShield();
	const float PartnerShieldBefore = Shooter->GetCurPartnerShield();
	const bool bDamageable = Shooter->CanBeDamaged() && !Shooter->IsDead();
	FOutlierDamageRequest Request;
	Request.DamageAmount = TestDamageAmount;
	Request.DamageTag = OutlierGameplayTags::Damage::Environment();
	Request.DamageOrigin = Shooter->GetActorLocation() + Shooter->GetActorForwardVector() * 100.0f;
	// 체력 값을 직접 바꾸지 않는다. 기존 피해 경로가 무적·실드·은신 해제·사망을 판정한다.
	const float Applied = OutlierDamage::Apply(Shooter, Request);
	const bool bVitalsUnchanged = HealthBefore == Shooter->GetCurHealth()
		&& ShieldBefore == Shooter->GetCurShield() && PartnerShieldBefore == Shooter->GetCurPartnerShield();
	// 피해 0만으로 무적이라고 판단하지 않는다. 적용 직전 태그와 적용 후 생명력도 함께 확인한다.
	const bool bBlockedByImmunity = bDamageImmune && bDamageable && Applied == 0.0f && bVitalsUnchanged;
	const FText Result = bBlockedByImmunity ? LOCTEXT("DamageImmuneBlocked", "무적으로 피해 차단")
		: Applied > 0.0f ? LOCTEXT("DamageAccepted", "피해 적용")
		: LOCTEXT("DamageRejected", "피해 미적용 (무적 차단 확인 불가)");
	UE_LOG(LogShooterSuitTester, Log,
		TEXT("[SuitTester][Damage] Result=%s Shooter=%s World=%s Time=%.3f DamageImmune=%d Damageable=%d Requested=%.2f Applied=%.2f HP=%.2f->%.2f Shield=%.2f->%.2f PartnerShield=%.2f->%.2f"),
		bBlockedByImmunity ? TEXT("BlockedByImmunity") : Applied > 0.0f ? TEXT("Applied") : TEXT("Rejected"),
		*Shooter->GetName(), *GetNameSafe(Shooter->GetWorld()), Shooter->GetWorld()->GetTimeSeconds(),
		bDamageImmune, bDamageable, Request.DamageAmount, Applied, HealthBefore, Shooter->GetCurHealth(),
		ShieldBefore, Shooter->GetCurShield(), PartnerShieldBefore, Shooter->GetCurPartnerShield());
	Status = FText::Format(LOCTEXT("DamageAppliedWithResult", "{0}: 요청 피해 {1}, 처리 피해 {2} / HP {3}, 실드 {4} / {5}"),
		FText::FromString(Shooter->GetName()), FText::AsNumber(TestDamageAmount), FText::AsNumber(Applied),
		FText::AsNumber(Shooter->GetCurHealth()), FText::AsNumber(Shooter->GetCurShield()), Result);
	return FReply::Handled();
}

FReply SOutlierShooterSuitTestToolWidget::StartLevel1Combat()
{
	if (!CanEdit()) return FReply::Handled();
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	UWorld* World = Shooter->GetWorld();
	ALevel1SuitUpgradeDoor* NearestDoor = nullptr;
	double NearestDistanceSq = TNumericLimits<double>::Max();
	// 검색은 버튼을 누를 때만 한다. 선택한 Shooter와 같은 서버 월드의 문만 사용한다.
	for (TActorIterator<ALevel1SuitUpgradeDoor> It(World); It; ++It)
	{
		if (!IsValid(*It) || !It->HasAuthority() || !IsValid(It->CombatRoomVolume)) continue;
		const double DistanceSq = FVector::DistSquared(Shooter->GetActorLocation(), It->GetActorLocation());
		if (DistanceSq < NearestDistanceSq)
		{
			NearestDoor = *It;
			NearestDistanceSq = DistanceSq;
		}
	}
	URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>();
	if (!NearestDoor || !Combat)
	{
		Status = LOCTEXT("NoLevel1Combat", "같은 PIE 서버에 CombatRoom이 설정된 Level1 문 또는 RoomCombatSubsystem이 없습니다.");
		return FReply::Handled();
	}
	const FGameplayTag RoomTag = NearestDoor->CombatRoomVolume->GetRoomTag();
	FRoomCombatTriggerContext Context;
	// 문 진행/저장 플래그를 조작하지 않고 문이 쓰는 전투 시작 API를 그대로 호출한다.
	// Room 준비 여부, 중복 시작, 다른 Room과의 충돌은 기존 서버 검증을 유지한다.
	const bool bStarted = Combat->HasReadyExternalTriggerRoster(NearestDoor->CombatRoomVolume, RoomTag)
		&& Combat->CreateTriggerContext(NearestDoor, RoomTag, FGameplayTag(), Context)
		&& Combat->StartTriggeredSequenceWithSpawnDelay(NearestDoor, Context, 3.0f);
	Status = bStarted
		? FText::Format(LOCTEXT("Level1CombatStarted", "{0} / {1}: 전투 시작 완료. 첫 Wave는 3초 뒤 소환됩니다."),
			FText::FromString(NearestDoor->GetName()), FText::FromString(RoomTag.ToString()))
		: FText::Format(LOCTEXT("Level1CombatRejected", "{0} / {1}: 시작 불가. Room·SpawnPoint 준비, 외부 트리거 차수, 진행 중이거나 완료된 전투를 확인하세요."),
			FText::FromString(NearestDoor->GetName()), FText::FromString(RoomTag.ToString()));
	return FReply::Handled();
}

FReply SOutlierShooterSuitTestToolWidget::RestoreOriginalState()
{
	if (CanEdit())
	{
		if (FOriginalState* Original = OriginalStates.Find(SelectedTarget->PlayerState))
		{
			StopAbilities();
			RemoveGrantedRifle(*Original);
			ApplyNodes(OutlierShooterSuitTest::RestoreNodes(SelectedTarget->PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter), Original->Nodes, Original->ControlledNodes));
			SelectedTarget->PlayerState->SetSuitMeshes(Original->FirstPersonSuitMesh.Get(), Original->ThirdPersonSuitMesh.Get());
			SelectedTarget->PlayerState->SetAcquiredSuit(Original->bSuit);
			SelectedTarget->Shooter->RefreshPresentationFromPlayerState();
			SelectedTarget->ASC->SetEditorShooterSuitGrantChecks(Original->bGrantChecks);
			OriginalStates.Remove(SelectedTarget->PlayerState);
			Status = LOCTEXT("Restored", "슈트와 테스트 대상 노드를 복원하고 도구가 지급한 소총을 회수했습니다. 다른 업그레이드와 노드 잔액은 유지합니다. 스킬 타이머는 되감지 않습니다.");
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
