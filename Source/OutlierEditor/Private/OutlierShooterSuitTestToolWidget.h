#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Save/OutlierLoadoutSnapshot.h"
#include "Widgets/SCompoundWidget.h"

class AShooterCharacter;
class AOutlierPlayerState;
class UOutlierAbilitySystemComponent;
class UOutlierUpgradeComponent;
class USkeletalMesh;
enum class ECheckBoxState : uint8;
template <typename OptionType> class SComboBox;

namespace OutlierShooterSuitTest
{
	// 구매 선행 조건/비용을 건너뛰되 실제 노드를 투영한다. 관련 없는 노드는 그대로 둔다.
	TArray<FName> ToggleNodes(const TArray<FName>& Current, const TArray<FName>& Matching, bool bEnabled);
	TArray<FName> RestoreNodes(const TArray<FName>& Current, const TArray<FName>& Original, const TArray<FName>& Controlled);
}

// 같은 에디터 프로세스에 있는 PIE 서버의 Shooter만 조작한다. 에셋/CDO는 수정하지 않는다.
class SOutlierShooterSuitTestToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOutlierShooterSuitTestToolWidget) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	virtual ~SOutlierShooterSuitTestToolWidget() override;

private:
	struct FTarget
	{
		TWeakObjectPtr<AShooterCharacter> Shooter;
		TWeakObjectPtr<AOutlierPlayerState> PlayerState;
		TWeakObjectPtr<UOutlierAbilitySystemComponent> ASC;
		TWeakObjectPtr<UOutlierUpgradeComponent> Upgrades;
		FText Label;
	};
	struct FFeature
	{
		FGameplayTag Tag;
		FText Label;
		FText Tooltip;
		TArray<FName> Nodes;
	};
	struct FOriginalState
	{
		TArray<FName> Nodes;
		TArray<FName> ControlledNodes;
		bool bSuit = false;
		bool bGrantChecks = false;
		TWeakObjectPtr<AWeaponBase> GrantedRifle;
		FOutlierLoadoutSnapshot BeforeRifleGrant;
		TWeakObjectPtr<USkeletalMesh> FirstPersonSuitMesh;
		TWeakObjectPtr<USkeletalMesh> ThirdPersonSuitMesh;
	};
	using FTargetPtr = TSharedPtr<FTarget>;

	void RefreshTargets();
	void HandlePIEChanged(bool bSimulating);
	void SelectTarget(FTargetPtr Target, ESelectInfo::Type SelectInfo);
	void RefreshFeatureNodes();
	bool CanEdit() const;
	ECheckBoxState GetFeatureState(int32 Index) const;
	void SetFeatureState(int32 Index, ECheckBoxState State);
	void SetSuitState(ECheckBoxState State);
	bool GrantSuitEquipment();
	void RemoveGrantedRifle(FOriginalState& Original);
	void CaptureOriginalState();
	FReply RestoreOriginalState();
	FReply DamageShooter();
	FReply StartLevel1Combat();
	void StopAbilities() const;
	void ApplyNodes(const TArray<FName>& Nodes) const;

	TArray<FTargetPtr> Targets;
	FTargetPtr SelectedTarget;
	TSharedPtr<SComboBox<FTargetPtr>> TargetCombo;
	TArray<FFeature> Features;
	TMap<TWeakObjectPtr<AOutlierPlayerState>, FOriginalState> OriginalStates;
	FText Status;
	float TestDamageAmount = 10.0f;
};
