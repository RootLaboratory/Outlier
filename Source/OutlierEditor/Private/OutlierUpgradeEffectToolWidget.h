#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Upgrade/OutlierUpgradeEffectTypes.h"
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
	FName EffectClassKey = NAME_None;
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
	using FNamePtr = TSharedPtr<FName>;
	using FStringPtr = TSharedPtr<FString>;

	// ── 데이터 소스 ──────────────────────────────────
	void RefreshDataTableChoices();
	void ReloadRowsFromEffectTable();
	void RefreshTargetTagOptions();       // 현재 EffectType 에 맞는 후보로 갱신
	void RefreshNodeRowNameOptions();
	void RefreshEffectClassKeyOptions();

	// ── 리스트 ───────────────────────────────────────
	TSharedRef<ITableRow> GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	void OnRowSelected(FRowPtr Row, ESelectInfo::Type SelectInfo);

	// ── 편집 반영 ────────────────────────────────────
	void PushEditToSelectedRow();          // 편집 필드 -> 선택 행
	void PullSelectedRowToEdit(const FOutlierUpgradeEffectToolRow& Row); // 선택 행 -> 편집 필드
	FName BuildAutoName() const;           // <Node>_<TargetLeaf> 자동 Name

	// ── 버튼 ─────────────────────────────────────────
	FReply OnAddRow();
	FReply OnDeleteRow();
	FReply OnSaveAndReimport();

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

	// 편집 필드 현재값
	FOutlierUpgradeEffectToolRow EditRow;
	bool bNameEditedManually = false;

	// 콤보 옵션들
	TArray<FStringPtr> EffectTypeLabelOptions;
	TArray<FStringPtr> OpLabelOptions;
	TArray<FNamePtr>   NodeRowNameOptions;
	TArray<FNamePtr>   TargetTagOptions;
	TArray<FNamePtr>   ConfigFieldOptions;
	TArray<FNamePtr>   EffectClassKeyOptions;

	// 콤보 위젯 refs ( 갱신용 )
	TSharedPtr<SComboBox<FNamePtr>> TargetTagCombo;
	TSharedPtr<SComboBox<FNamePtr>> NodeRowNameCombo;
	TSharedPtr<SComboBox<FNamePtr>> EffectClassKeyCombo;

	FText StatusText;
};
