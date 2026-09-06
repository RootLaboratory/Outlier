#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Upgrade/OutlierUpgradeEffectTypes.h"
#include "Upgrade/OutlierUpgradeTypes.h"
#include "Widgets/SCompoundWidget.h"

class UDataTable;
class STableViewBase;
class ITableRow;
template <typename ItemType> class SListView;
template <typename OptionType> class SComboBox;

// 편집 중인 효과 행 하나 ( FOutlierUpgradeEffectRow + RowName ).
struct FOutlierUpgradeEffectToolRow
{
	FName Name = NAME_None;          // 효과 테이블의 RowName ( 고유 키 )
	FName NodeRowName = NAME_None;
	EOutlierUpgradeEffectType EffectType = EOutlierUpgradeEffectType::Attribute;
	FGameplayTag TargetTag;
	FName ConfigField = NAME_None;
	EOutlierUpgradeModOp Op = EOutlierUpgradeModOp::Additive;
	float Magnitude = 0.0f;
};

// 편집 중인 Node 행 하나 ( FOutlierUpgradeNodeRow + RowName ).
// NodeRowName 리네임 시, Effect 쪽 NodeRowName / Node 쪽 ParentId 와 동기화하기 위해
// Effect Rows 처럼 in-memory 로 들고 있는다.
struct FOutlierUpgradeNodeToolRow
{
	FName RowName = NAME_None;       // Node 테이블의 RowName ( 고유 키 )
	EOutlierUpgradeRole Role = EOutlierUpgradeRole::None;
	FName TreeId = NAME_None;
	FName NodeId = NAME_None;
	FName ParentId = NAME_None;      // 부모 Node 의 RowName
	FGameplayTag AbilityTag;
	int32 Cost = 0;
	FText DisplayName;
	FText Desc;
};

// 기획자용 Upgrade 효과 편집 툴.
// 드롭다운(친화 용어)으로 고르면 raw CSV( (TagName=...) 포함 )로 써주고 DataTable 을 Reimport 한다.
class SOutlierUpgradeEffectToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOutlierUpgradeEffectToolWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FRowPtr = TSharedPtr<FOutlierUpgradeEffectToolRow>;
	using FNodeRowPtr = TSharedPtr<FOutlierUpgradeNodeToolRow>;
	using FNamePtr = TSharedPtr<FName>;
	using FStringPtr = TSharedPtr<FString>;

	// ── 데이터 소스 ──────────────────────────────────
	void RefreshDataTableChoices();
	void ReloadRowsFromEffectTable();
	void ReloadRowsFromNodeTable();
	void RefreshTargetTagOptions();       // 현재 EffectType(+FunctionOverride 인 경우 Node 의 AbilityTag) 에 맞는 후보로 갱신
	void RefreshNodeRowNameOptions();     // NodeRows ( in-memory ) 기준으로 갱신 ( 저장 전 리네임도 즉시 반영되도록 )
	void RefreshParentIdOptions();        // ParentId 콤보: NodeRows 중 자기 자신을 뺀 목록 + "(없음)"
	void RefreshNodeAbilityTagOptions();  // AbilityTag 콤보: "Ability" 프리픽스 하위 전체
	void RefreshConfigFieldOptions();     // ConfigField 콤보: Node 의 Role 에 맞는 AbilityConfig struct 필드로 갱신
	FGameplayTag GetSelectedNodeAbilityTag() const; // EditRow.NodeRowName 이 가리키는 Node 의 AbilityTag
	EOutlierUpgradeRole GetSelectedNodeRole() const; // EditRow.NodeRowName 이 가리키는 Node 의 Role

	// ── 리스트 ───────────────────────────────────────
	TSharedRef<ITableRow> GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void OnRowSelected(FRowPtr Row, ESelectInfo::Type SelectInfo);
	TSharedRef<ITableRow> GenerateNodeRow(FNodeRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void OnNodeRowSelected(FNodeRowPtr Row, ESelectInfo::Type SelectInfo);

	// ── 편집 반영 ────────────────────────────────────
	void PushEditToSelectedRow();          // 편집 필드 -> 선택 행
	void PullSelectedRowToEdit(const FOutlierUpgradeEffectToolRow& Row); // 선택 행 -> 편집 필드
	void PullSelectedNodeToEdit(const FOutlierUpgradeNodeToolRow& Row);  // 선택 Node -> Node 편집 필드
	FName BuildAutoName() const;           // <Node>_<TargetLeaf> 자동 Name
	void SelectNodeInListByRowName(FName InRowName); // Node 패널로 전환할 때 현재 Effect가 물고 있는 Node를 미리 선택

	// ── 버튼 ─────────────────────────────────────────
	FReply OnAddRow();
	FReply OnDeleteRow();
	FReply OnAddOrUpdateNodeRow();         // NodeEditRow.RowName 이 기존에 있으면 필드 수정, 없으면 새로 추가 ( 리네임 아님 )
	FReply OnDeleteNodeRow();
	FReply OnRenameNodeRow();              // NodeRowName 리네임: Node 의 ParentId, Effect 의 NodeRowName 을 함께 치환
	FReply OnSaveAndReimport();
	FReply OnSwitchToEffectPanel();
	FReply OnSwitchToNodePanel();
	bool SaveEffectTableCsv(FString& OutSavedFileName);
	bool SaveNodeTableCsv(FString& OutSavedFileName);

	void SetStatus(const FText& InText);

	// ── 위젯 상태 ────────────────────────────────────
	TWeakObjectPtr<UDataTable> EffectTable;
	TWeakObjectPtr<UDataTable> NodeTable;

	TArray<TWeakObjectPtr<UDataTable>> EffectTableChoices;
	TArray<TWeakObjectPtr<UDataTable>> NodeTableChoices;
	TArray<TSharedPtr<FString>> EffectTableChoiceLabels;
	TArray<TSharedPtr<FString>> NodeTableChoiceLabels;

	TArray<FRowPtr> Rows;
	TSharedPtr<SListView<FRowPtr>> RowListView;

	TArray<FNodeRowPtr> NodeRows;         // Node 테이블 in-memory 사본 ( 리네임/ParentId 동기화용 )
	TSharedPtr<SListView<FNodeRowPtr>> NodeListView;

	bool bShowNodePanel = false;          // false = Effect 패널, true = Node 패널 ( 기본은 Effect )

	// 편집 필드 현재값
	FOutlierUpgradeEffectToolRow EditRow;
	bool bNameEditedManually = false;
	FOutlierUpgradeNodeToolRow NodeEditRow; // Node 패널에서 편집 중인 값
	FString NodeRenameInputText;           // NodeRowName 리네임 / 새 Node 추가용 이름 입력값

	// 콤보 옵션들
	TArray<FStringPtr> EffectTypeLabelOptions;
	TArray<FStringPtr> OpLabelOptions;
	TArray<FStringPtr> RoleLabelOptions;
	TArray<FNamePtr>   NodeRowNameOptions;
	TArray<FNamePtr>   TargetTagOptions;
	TArray<FNamePtr>   ConfigFieldOptions;
	TArray<FNamePtr>   ParentIdOptions;
	TArray<FNamePtr>   NodeAbilityTagOptions;

	// 콤보 위젯 refs ( 갱신용 )
	TSharedPtr<SComboBox<FNamePtr>> TargetTagCombo;
	TSharedPtr<SComboBox<FNamePtr>> NodeRowNameCombo;
	TSharedPtr<SComboBox<FNamePtr>> ParentIdCombo;
	TSharedPtr<SComboBox<FNamePtr>> NodeAbilityTagCombo;
	TSharedPtr<SComboBox<FNamePtr>> ConfigFieldCombo;

	FText StatusText;
};
