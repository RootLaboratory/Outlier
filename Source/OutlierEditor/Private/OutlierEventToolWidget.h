#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class STableViewBase;
class UWorld;
enum class EOutlierPlayerRole : uint8;
template <typename ItemType> class SListView;
template <typename OptionType> class SComboBox;

// Event DT 한 행을 표시용으로 펼친 것.
struct FOutlierEventToolRow
{
	FGameplayTag EventTag;
	FString Script;   // TTS 대사 첫 줄
	FString Upper;    // "Input: 텍스트" / "Goal: 텍스트"
	FString Gif;      // GIF Subject
	FString Warning;  // 데이터 누락 요약
};

// 요청 출발점. World가 비어 있으면 준비된 PIE 플레이어 하나를 선택한다. 서버는 양쪽에 전송한다.
struct FOutlierEventToolTarget
{
	TWeakObjectPtr<UWorld> World;
	EOutlierPlayerRole Role{};
	FText Label;
};

// 역할별 Event DT를 미리 보고, 선택한 EventID를 서버에 한 번 요청한다.
class SOutlierEventToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOutlierEventToolWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SOutlierEventToolWidget() override;

private:
	using FRowPtr = TSharedPtr<FOutlierEventToolRow>;
	using FTargetPtr = TSharedPtr<FOutlierEventToolTarget>;

	// ── 데이터 ───────────────────────────────────────
	void ReloadRows();
	void RefreshFilteredRows();
	void SetRole(EOutlierPlayerRole NewRole);

	// ── 대상 ─────────────────────────────────────────
	void RefreshTargets();
	TArray<UWorld*> GetTargetWorlds() const;
	void HandlePIEChanged(bool bIsSimulating);
	TSharedRef<SWidget> GenerateTargetItem(FTargetPtr Target) const;
	void OnTargetSelected(FTargetPtr Target, ESelectInfo::Type SelectInfo);

	// ── 리스트 / 전송 ────────────────────────────────
	TSharedRef<ITableRow> GenerateRow(FRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable);
	bool CanSend() const;
	void SendSelectedEvent();

	TArray<FRowPtr> Rows;
	TArray<FRowPtr> FilteredRows;
	TArray<FTargetPtr> Targets;
	FTargetPtr AllTarget;
	FTargetPtr SelectedTarget;
	TSharedPtr<SListView<FRowPtr>> RowListView;
	TSharedPtr<SComboBox<FTargetPtr>> TargetComboBox;
	EOutlierPlayerRole Role{};
	FString SearchText;
	FText StatusText;
};
