#include "OutlierShooterSuitTestToolWidget.h"

#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/SuitInteraction.h"
#include "Interaction/Level1SuitUpgradeDoor.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"
#include "Shooter/ShooterInventoryComponent.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomVolume.h"
#include "Room/RoomTagComponent.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "UI/UILayerGameplayTags.h"
#include "UI/UILayerTypes.h"
#include "UI/UpgradeNodeGroupWidget.h"
#include "Weapon/RangedWeaponBase.h"
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
		.ToolTipText(LOCTEXT("SuitTip", "Shooter 슈트 메시·소총과 Partner 슈트 해금·무기를 함께 적용합니다. 장비 기록 또는 맵의 SuitInteraction 설정을 사용하며, 해제 시 도구가 추가한 무기만 회수합니다."))
		[SNew(STextBlock).Text(LOCTEXT("Suit", "슈트 획득"))]
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("OpenUpgradeUI", "노드 업그레이드 UI 열기"))
			.ToolTipText(LOCTEXT("OpenUpgradeUITip", "선택한 Shooter와 짝 Partner의 화면에 각각 노드 업그레이드 UI를 엽니다. 구매 비용과 선행 조건은 그대로 적용됩니다."))
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.OnClicked(this, &SOutlierShooterSuitTestToolWidget::OpenUpgradeUI)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("AddUpgradeNodes", "노드 +100"))
			.ToolTipText(LOCTEXT("AddUpgradeNodesTip", "선택한 Shooter와 짝 Partner에게 각각 노드 100개를 추가합니다. 복원 버튼을 눌러도 잔액은 유지됩니다."))
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.OnClicked(this, &SOutlierShooterSuitTestToolWidget::AddUpgradeNodes)
		]
	];
	Rows->AddSlot().AutoHeight().Padding(6.0f)
	[
		SNew(SCheckBox)
		.IsEnabled(this, &SOutlierShooterSuitTestToolWidget::CanEditInvulnerability)
		.IsChecked(this, &SOutlierShooterSuitTestToolWidget::GetInvulnerabilityState)
		.OnCheckStateChanged(this, &SOutlierShooterSuitTestToolWidget::SetInvulnerabilityState)
		.ToolTipText(LOCTEXT("PairInvulnerableTip", "선택한 페어의 모든 피해를 막습니다. 재부팅·프리셋 재시작·창 재오픈 후에도 유지하며, 직접 끄거나 PIE를 끝내면 해제됩니다. 별도 프로세스 서버는 지원하지 않습니다."))
		[SNew(STextBlock).Text(LOCTEXT("PairInvulnerable", "Shooter·Partner 무적"))]
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
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).Text(LOCTEXT("GrantPistol", "권총 획득"))
			.ToolTipText(LOCTEXT("GrantPistolTip", "선택한 Shooter에게 BP_Pistol을 지급합니다. 이미 권총을 보유했다면 중복 지급하지 않습니다. 지급한 권총은 복원 버튼으로 회수하지 않습니다."))
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.OnClicked(this, &SOutlierShooterSuitTestToolWidget::GrantPistol)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f)
		[
			SNew(SButton).Text(LOCTEXT("FinishCurrentRoomCombat", "현재 방 전투 종료 및 정리"))
			.ToolTipText(LOCTEXT("FinishCurrentRoomCombatTip", "선택한 Shooter가 있는 방의 적·빙의된 적·Wave 터렛과 예약 소환을 정리하고 방 전체를 완료합니다. 다음 차수도 생략하며 완료 저장과 출입구 해제가 적용됩니다. 복원할 수 없습니다."))
			.IsEnabled_Lambda([this]() { return CanEdit(); })
			.OnClicked(this, &SOutlierShooterSuitTestToolWidget::FinishCurrentRoomCombat)
		]
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
	const TWeakObjectPtr<AOutlierPlayerState> Previous = SelectedTarget.IsValid() ? SelectedTarget->PlayerState : nullptr;
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
				if (Target->PlayerState == Previous)
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

bool SOutlierShooterSuitTestToolWidget::CanEditInvulnerability() const
{
	const AOutlierPlayerState* State = SelectedTarget.IsValid() ? SelectedTarget->PlayerState.Get() : nullptr;
	return State && State->HasAuthority() && State->IsShooterPlayer() && State->GetPairId() != INDEX_NONE
		&& State->GetWorld() && State->GetWorld()->WorldType == EWorldType::PIE;
}

ECheckBoxState SOutlierShooterSuitTestToolWidget::GetInvulnerabilityState() const
{
	return CanEditInvulnerability() && SelectedTarget->PlayerState->IsEditorPairInvulnerable()
		? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOutlierShooterSuitTestToolWidget::SetInvulnerabilityState(ECheckBoxState State)
{
	if (!CanEditInvulnerability() || State == ECheckBoxState::Undetermined) return;
	const bool bEnabled = State == ECheckBoxState::Checked;
	if (SelectedTarget->PlayerState->SetEditorPairInvulnerable(bEnabled))
	{
		// 테스트 전 상태 복원과 독립적이다. 사용자가 토글을 끄기 전까지 유지한다.
		Status = bEnabled ? LOCTEXT("PairInvulnerableOn", "선택한 페어 무적 ON: Shooter·Partner의 모든 피해를 차단합니다.")
			: LOCTEXT("PairInvulnerableOff", "선택한 페어 무적 OFF: 기존 피해·면역 규칙을 적용합니다.");
	}
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
	if (!Original.PartnerPlayerState.IsValid())
	{
		if (APartnerCharacter* Partner = SelectedTarget->PlayerState->GetPartnerCharacter())
		{
			if (AOutlierPlayerState* PartnerPS = Partner->GetPlayerState<AOutlierPlayerState>())
			{
				Original.PartnerPlayerState = PartnerPS;
				Original.bPartnerSuit = PartnerPS->GetAcquiredSuit();
			}
		}
	}
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
		RemoveGrantedPartnerWeapon(OriginalStates.FindChecked(SelectedTarget->PlayerState));
	}
	SelectedTarget->ASC->SetEditorShooterSuitGrantChecks(true);
	SelectedTarget->PlayerState->SetAcquiredSuit(State == ECheckBoxState::Checked);
	if (AOutlierPlayerState* PartnerPS = OriginalStates.FindChecked(SelectedTarget->PlayerState).PartnerPlayerState.Get())
	{
		PartnerPS->SetAcquiredSuit(State == ECheckBoxState::Checked);
	}
	Status = LOCTEXT("SuitApplied", "페어의 슈트 상태와 Shooter 외형을 적용했습니다. Shooter 소총·Partner 무기를 지급하며, 해제 시 도구가 추가한 무기만 회수합니다.");
}

bool SOutlierShooterSuitTestToolWidget::GrantSuitEquipment()
{
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	APartnerCharacter* Partner = SelectedTarget->PlayerState->GetPartnerCharacter();
	if (!IsValid(Partner) || !Partner->HasAuthority() || Partner->GetWorld() != Shooter->GetWorld()
		|| !Partner->GetPlayerState<AOutlierPlayerState>())
	{
		Status = LOCTEXT("NoPartner", "같은 PIE 서버에 페어 Partner와 PlayerState가 있어야 슈트를 지급할 수 있습니다.");
		return false;
	}
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
	AWeaponBase* PartnerWeapon = Partner->GetCurrentWeapon();
	TSubclassOf<AWeaponBase> PartnerWeaponClass = SavedLoadout.PartnerWeaponClass;
	if (!PartnerWeaponClass && SuitSource) PartnerWeaponClass = SuitSource->GetEditorPartnerWeaponClass();
	if (!PartnerWeapon && (!PartnerWeaponClass || !PartnerWeaponClass->IsChildOf(ARangedWeaponBase::StaticClass())))
	{
		Status = LOCTEXT("NoPartnerWeaponConfiguration", "Partner 무기 설정을 찾지 못했습니다. 장비 기록 또는 맵의 SuitInteraction에 PartnerWeaponClass가 필요합니다.");
		return false;
	}
	// 양쪽 지급을 확정하기 전 실패하면 새 Partner 무기만 정리한다. 기존 장비는 건드리지 않는다.
	AWeaponBase* PendingPartnerWeapon = nullptr;
	ON_SCOPE_EXIT
	{
		if (PendingPartnerWeapon) PendingPartnerWeapon->Destroy();
	};

	USkeletalMesh* FirstPersonMesh = SuitSource ? SuitSource->GetEditorShooterFirstPersonMesh() : SelectedTarget->PlayerState->GetSuitFirstPersonMesh();
	USkeletalMesh* ThirdPersonMesh = SuitSource ? SuitSource->GetEditorShooterThirdPersonMesh() : SelectedTarget->PlayerState->GetSuitThirdPersonMesh();
	if (!Shooter->ApplySuitMeshes(FirstPersonMesh, ThirdPersonMesh))
	{
		Status = LOCTEXT("InvalidSuitMeshes", "슈트 외형 적용에 실패했습니다. Shooter BP의 SuitPresentation 메시·ABP 설정을 확인하세요.");
		return false;
	}

	FOriginalState& Original = OriginalStates.FindChecked(SelectedTarget->PlayerState);
	if (!PartnerWeapon)
	{
		PendingPartnerWeapon = AWeaponBase::SpawnLoadoutWeapon(Shooter->GetWorld(), PartnerWeaponClass, Partner);
		if (!PendingPartnerWeapon)
		{
			Shooter->RefreshPresentationFromPlayerState();
			Status = LOCTEXT("PartnerWeaponGrantFailed", "Partner 무기 생성에 실패했습니다. 슈트 획득 상태는 변경하지 않았습니다.");
			return false;
		}
	}
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
	if (PendingPartnerWeapon)
	{
		Original.BeforePartnerWeaponClass = SelectedTarget->PlayerState->GetLoadoutSnapshot().PartnerWeaponClass;
		Partner->EquipWeapon(PendingPartnerWeapon);
		PendingPartnerWeapon->ShowEquippedPresentation();
		Original.GrantedPartnerWeapon = PendingPartnerWeapon;
		Original.GrantedPartnerWeaponClass = PendingPartnerWeapon->GetClass();
		// 실제 슈트 획득과 동일하게 리로드용 Partner 무기 클래스를 Shooter PS에 남긴다.
		FOutlierLoadoutSnapshot Snapshot = SelectedTarget->PlayerState->GetLoadoutSnapshot();
		Snapshot.PartnerWeaponClass = PendingPartnerWeapon->GetClass();
		SelectedTarget->PlayerState->SetLoadoutSnapshot(Snapshot);
		PendingPartnerWeapon = nullptr;
	}
	// PS의 획득 상태가 외형을 다시 투영할 때도 실제 적용한 메시를 사용하도록 맞춘다.
	SelectedTarget->PlayerState->SetSuitMeshes(Shooter->GetFirstPersonMesh()->GetSkeletalMeshAsset(), Shooter->GetMesh()->GetSkeletalMeshAsset());
	return true;
}

void SOutlierShooterSuitTestToolWidget::RemoveGrantedPartnerWeapon(FOriginalState& Original)
{
	if (AWeaponBase* Weapon = Original.GrantedPartnerWeapon.Get())
	{
		if (APartnerCharacter* Partner = Cast<APartnerCharacter>(Weapon->GetOwner()))
		{
			if (Partner->GetCurrentWeapon() == Weapon) Partner->EquipWeapon(nullptr);
		}
		Weapon->Destroy();
	}
	if (Original.GrantedPartnerWeaponClass)
	{
		FOutlierLoadoutSnapshot Snapshot = SelectedTarget->PlayerState->GetLoadoutSnapshot();
		if (Snapshot.PartnerWeaponClass == Original.GrantedPartnerWeaponClass)
		{
			Snapshot.PartnerWeaponClass = Original.BeforePartnerWeaponClass;
			SelectedTarget->PlayerState->SetLoadoutSnapshot(Snapshot);
		}
	}
	Original.GrantedPartnerWeapon.Reset();
	Original.GrantedPartnerWeaponClass = nullptr;
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
	const bool bTesterImmune = OutlierDamage::IsEditorInvulnerable(Shooter);
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
	const bool bBlockedByImmunity = (bDamageImmune || bTesterImmune) && bDamageable && Applied == 0.0f && bVitalsUnchanged;
	const FText Result = bBlockedByImmunity && bTesterImmune ? LOCTEXT("TesterDamageBlocked", "Tester 무적으로 피해 차단")
		: bBlockedByImmunity ? LOCTEXT("DamageImmuneBlocked", "무적으로 피해 차단")
		: Applied > 0.0f ? LOCTEXT("DamageAccepted", "피해 적용")
		: LOCTEXT("DamageRejected", "피해 미적용 (무적 차단 확인 불가)");
	UE_LOG(LogShooterSuitTester, Log,
		TEXT("[SuitTester][Damage] Result=%s Shooter=%s World=%s Time=%.3f DamageImmune=%d TesterImmune=%d Damageable=%d Requested=%.2f Applied=%.2f HP=%.2f->%.2f Shield=%.2f->%.2f PartnerShield=%.2f->%.2f"),
		bBlockedByImmunity ? TEXT("BlockedByImmunity") : Applied > 0.0f ? TEXT("Applied") : TEXT("Rejected"),
		*Shooter->GetName(), *GetNameSafe(Shooter->GetWorld()), Shooter->GetWorld()->GetTimeSeconds(),
		bDamageImmune, bTesterImmune, bDamageable, Request.DamageAmount, Applied, HealthBefore, Shooter->GetCurHealth(),
		ShieldBefore, Shooter->GetCurShield(), PartnerShieldBefore, Shooter->GetCurPartnerShield());
	Status = FText::Format(LOCTEXT("DamageAppliedWithResult", "{0}: 요청 피해 {1}, 처리 피해 {2} / HP {3}, 실드 {4} / {5}"),
		FText::FromString(Shooter->GetName()), FText::AsNumber(TestDamageAmount), FText::AsNumber(Applied),
		FText::AsNumber(Shooter->GetCurHealth()), FText::AsNumber(Shooter->GetCurShield()), Result);
	return FReply::Handled();
}

FReply SOutlierShooterSuitTestToolWidget::OpenUpgradeUI()
{
	if (!CanEdit()) return FReply::Handled();
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(Shooter->GetController());
	const TSubclassOf<UUpgradeNodeGroupWidget> WidgetClass = LoadClass<UUpgradeNodeGroupWidget>(nullptr,
		TEXT("/Game/Blueprints/Widget/FirstPerson/Upgrade/WBP_ShooterNodeGroup.WBP_ShooterNodeGroup_C"));
	if (!Controller || !WidgetClass)
	{
		Status = LOCTEXT("UpgradeUIUnavailable", "Shooter 컨트롤러 또는 WBP_ShooterNodeGroup을 찾지 못했습니다.");
		return FReply::Handled();
	}

	APartnerCharacter* Partner = Shooter->GetPartnerCharacter();
	// 각 UI는 해당 캐릭터의 소유 클라이언트에서 만든다. 두 화면에 같은 Pair 문맥을 전달한다.
	FUILayerPushRequest Request;
	Request.WidgetClass = WidgetClass;
	Request.LayerTag = UILayerTags::GameMenu();
	Request.InputModeTag = FirstPersonInputModeTags::UI();
	Request.RequestOwner = Shooter;
	Request.ContextActors = {Shooter, Partner};
	Controller->ClientPushUILayer(Request);
	AFirstPersonPlayerController* PartnerController = Partner
		? Cast<AFirstPersonPlayerController>(Partner->GetController()) : nullptr;
	if (!PartnerController)
	{
		Status = LOCTEXT("UpgradePartnerUnavailable", "Shooter UI 열기를 요청했습니다. 짝 Partner의 컨트롤러를 찾지 못해 Partner UI는 열지 못했습니다.");
		return FReply::Handled();
	}
	const TSubclassOf<UUpgradeNodeGroupWidget> PartnerWidgetClass = LoadClass<UUpgradeNodeGroupWidget>(nullptr,
		TEXT("/Game/Blueprints/Widget/FirstPerson/Upgrade/WBP_PartnerNodeGroup.WBP_PartnerNodeGroup_C"));
	if (!PartnerWidgetClass)
	{
		Status = LOCTEXT("UpgradePartnerWidgetUnavailable", "Shooter UI 열기를 요청했습니다. WBP_PartnerNodeGroup을 찾지 못해 Partner UI는 열지 못했습니다.");
		return FReply::Handled();
	}
	Request.WidgetClass = PartnerWidgetClass;
	Request.RequestOwner = Partner;
	PartnerController->ClientPushUILayer(Request);
	Status = LOCTEXT("UpgradeUIRequested", "Shooter와 짝 Partner에게 각각 노드 업그레이드 UI 열기를 요청했습니다. 각 PIE 창에서 확인하세요.");
	return FReply::Handled();
}

FReply SOutlierShooterSuitTestToolWidget::AddUpgradeNodes()
{
	if (!CanEdit()) return FReply::Handled();
	// 기존 서버 지급 경로를 사용해 잔액 통보와 소유 클라이언트 복제를 유지한다.
	AOutlierPlayerState* PlayerState = SelectedTarget->PlayerState.Get();
	APartnerCharacter* Partner = SelectedTarget->Shooter->GetPartnerCharacter();
	AOutlierPlayerState* PartnerState = Partner ? Partner->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const bool bShooterAdded = PlayerState->AddNode(100);
	const bool bPartnerAdded = IsValid(PartnerState) && PartnerState != PlayerState
		&& PartnerState->IsPartnerPlayer() && PartnerState->GetPairId() == PlayerState->GetPairId()
		&& PartnerState->AddNode(100);
	// 한쪽 지급만 성공한 경우에도 실제 결과를 표시해 중복 충전을 피할 수 있게 한다.
	const FText ShooterResult = bShooterAdded
		? LOCTEXT("UpgradeNodesGranted", "+100 완료") : LOCTEXT("UpgradeNodesNotGranted", "추가 실패");
	const FText PartnerResult = bPartnerAdded
		? LOCTEXT("UpgradeNodesGranted", "+100 완료") : LOCTEXT("UpgradeNodesNotGranted", "추가 실패");
	Status = FText::Format(LOCTEXT("UpgradePairNodesResult", "Shooter: {0} / 현재 {1}개 · Partner: {2} / 현재 {3}"),
		ShooterResult, FText::AsNumber(PlayerState->GetNodeCount()), PartnerResult,
		IsValid(PartnerState) ? FText::AsNumber(PartnerState->GetNodeCount()) : LOCTEXT("UpgradePartnerNodesUnavailable", "확인 불가"));
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

FReply SOutlierShooterSuitTestToolWidget::GrantPistol()
{
	if (!CanEdit()) return FReply::Handled();
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	UShooterInventoryComponent* Inventory = Shooter->GetInventoryComponent();
	if (!Inventory)
	{
		Status = LOCTEXT("PistolNoInventory", "Shooter 인벤토리가 없어 권총을 지급하지 못했습니다.");
		return FReply::Handled();
	}
	if (AWeaponBase* Existing = Inventory->GetWeaponInSlot(EWeaponSlot::Secondary))
	{
		Status = Existing->GetWeaponType() == EWeaponType::Pistol
			? LOCTEXT("PistolAlreadyOwned", "이미 권총을 보유하고 있습니다. 중복 지급하지 않았습니다.")
			: LOCTEXT("PistolSlotOccupied", "Secondary 슬롯에 다른 무기가 있어 권총을 지급하지 않았습니다.");
		return FReply::Handled();
	}
	const TSubclassOf<AWeaponBase> PistolClass = LoadClass<AWeaponBase>(nullptr,
		TEXT("/Game/Blueprints/Weapon/BP_Pistol.BP_Pistol_C"));
	AWeaponBase* Pistol = PistolClass ? AWeaponBase::SpawnLoadoutWeapon(Shooter->GetWorld(), PistolClass, Shooter) : nullptr;
	// PIE 초기화 중 눌러도 DataTable의 무기 타입을 읽은 뒤에만 슬롯에 반영한다.
	if (Pistol && !Pistol->HasActorBegunPlay()) Pistol->DispatchBeginPlay();
	if (!Pistol || Pistol->GetWeaponType() != EWeaponType::Pistol)
	{
		if (Pistol) Pistol->Destroy();
		Status = LOCTEXT("PistolSpawnFailed", "BP_Pistol 생성 또는 권총 타입 확인에 실패했습니다.");
		return FReply::Handled();
	}
	// 실제 획득과 같은 장착 경로가 슬롯/PS 기록과 소유 클라이언트 복제를 함께 갱신한다.
	Shooter->EquipWeapon(Pistol);
	if (Inventory->GetWeaponInSlot(EWeaponSlot::Secondary) != Pistol)
	{
		Pistol->Destroy();
		Status = LOCTEXT("PistolEquipFailed", "권총 장착이 거부되어 지급하지 않았습니다. 사망·장착 잠금 상태를 확인하세요.");
		return FReply::Handled();
	}
	Status = LOCTEXT("PistolGranted", "권총 획득 완료. 장비 기록에 반영했으며 복원 버튼으로 회수하지 않습니다.");
	return FReply::Handled();
}

FReply SOutlierShooterSuitTestToolWidget::FinishCurrentRoomCombat()
{
	if (!CanEdit()) return FReply::Handled();
	AShooterCharacter* Shooter = SelectedTarget->Shooter.Get();
	URoomTagComponent* RoomTags = Shooter->GetRoomTagComp();
	const FGameplayTag RoomTag = RoomTags ? RoomTags->GetCurrentRoomTag() : FGameplayTag();
	URoomCombatSubsystem* Combat = Shooter->GetWorld()->GetSubsystem<URoomCombatSubsystem>();
	// 다른 방의 진행을 실수로 끝내지 않도록 전역 활성 방이나 가장 가까운 방으로 대체하지 않는다.
	const bool bFinished = RoomTag.IsValid() && Combat && Combat->FinishRoomCombatForEditor(RoomTag);
	Status = bFinished
		? FText::Format(LOCTEXT("CurrentRoomFinished", "{0}: 방 전투 종료 및 정리 완료. 남은 차수는 생략했습니다."), FText::FromString(RoomTag.ToString()))
		: FText::Format(LOCTEXT("CurrentRoomFinishRejected", "{0}: 정리 불가. 현재 방 등록·전투 설정·이미 완료된 상태인지 확인하세요."), FText::FromString(RoomTag.ToString()));
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
			RemoveGrantedPartnerWeapon(*Original);
			ApplyNodes(OutlierShooterSuitTest::RestoreNodes(SelectedTarget->PlayerState->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter), Original->Nodes, Original->ControlledNodes));
			SelectedTarget->PlayerState->SetSuitMeshes(Original->FirstPersonSuitMesh.Get(), Original->ThirdPersonSuitMesh.Get());
			SelectedTarget->PlayerState->SetAcquiredSuit(Original->bSuit);
			if (AOutlierPlayerState* PartnerPS = Original->PartnerPlayerState.Get())
			{
				PartnerPS->SetAcquiredSuit(Original->bPartnerSuit);
			}
			SelectedTarget->Shooter->RefreshPresentationFromPlayerState();
			SelectedTarget->ASC->SetEditorShooterSuitGrantChecks(Original->bGrantChecks);
			OriginalStates.Remove(SelectedTarget->PlayerState);
			Status = LOCTEXT("Restored", "페어의 슈트와 테스트 대상 노드를 복원하고 도구가 지급한 무기를 회수했습니다. 다른 업그레이드와 노드 잔액은 유지합니다. 스킬 타이머는 되감지 않습니다.");
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
