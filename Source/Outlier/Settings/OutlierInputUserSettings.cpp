#include "Settings/OutlierInputUserSettings.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"
#include "Input/OutlierInputBindingTable.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedActionKeyMapping.h"

namespace
{
	EOutlierSprintInputMode NormalizeSprintInputMode(EOutlierSprintInputMode Mode)
	{
		return Mode == EOutlierSprintInputMode::Hold
			? EOutlierSprintInputMode::Hold : EOutlierSprintInputMode::Toggle;
	}
}

void UOutlierInputUserSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		// 한 역할의 손상된 값 때문에 다른 역할이나 기존 키 프로필을 초기화하지 않는다.
		ShooterSprintInputMode = NormalizeSprintInputMode(ShooterSprintInputMode);
		PartnerSprintInputMode = NormalizeSprintInputMode(PartnerSprintInputMode);
	}
}

EOutlierSprintInputMode UOutlierInputUserSettings::GetSprintInputMode(EOutlierPlayerRole Role) const
{
	if (Role == EOutlierPlayerRole::Shooter) return NormalizeSprintInputMode(ShooterSprintInputMode);
	if (Role == EOutlierPlayerRole::Partner) return NormalizeSprintInputMode(PartnerSprintInputMode);
	return EOutlierSprintInputMode::Toggle;
}

bool UOutlierInputUserSettings::SetSprintInputMode(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode)
{
	if (Mode != EOutlierSprintInputMode::Toggle && Mode != EOutlierSprintInputMode::Hold) return false;
	if (Role == EOutlierPlayerRole::Shooter) ShooterSprintInputMode = Mode;
	else if (Role == EOutlierPlayerRole::Partner) PartnerSprintInputMode = Mode;
	else return false;
	return true;
}

void UOutlierInputUserSettings::HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettingsObject)
{
	// 기본 구현이 현재 키 프로필과 리바인딩을 이전한다. 새 역할 필드는 생성 기본값 Toggle을 유지한다.
	Super::HandleSettingsClassChangedOnLoad_Impl(OldSettingsObject);
}

namespace
{
	FMapPlayerKeyArgs BindingArgs(const FOutlierInputBindingTableRow& Row)
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = Row.MappingName;
		Args.Slot = Row.Slot;
		Args.bCreateMatchingSlotIfNeeded = false;
		return Args;
	}

	bool OwnsProfileSlot(const TArray<FOutlierInputBindingTableRow>& Members,
		FName Name, EPlayerMappableKeySlot Slot)
	{
		return Members.ContainsByPredicate([Name, Slot](const auto& Member)
		{
			return Member.MappingName == Name && Member.Slot == Slot;
		});
	}
}

bool UOutlierInputUserSettings::RegisterBindingContexts(const UOutlierInputBindingTable* Table)
{
	if (!Table) return false;
	bool bReady = true;
	for (const auto& Row : Table->GetAllRowsSorted())
	{
		UInputMappingContext* Context = Row.MappingContext.LoadSynchronous();
		if (!Context) bReady = false;
		else RegisterInputMappingContext(Context);
	}
	return bReady && GetActiveKeyProfile();
}

bool UOutlierInputUserSettings::GetBindingKey(const FOutlierInputBindingTableRow& Row, FKey& OutKey) const
{
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	const FPlayerKeyMapping* Mapping = Profile ? Profile->FindKeyMapping(BindingArgs(Row)) : nullptr;
	if (!Mapping) return false;
	OutKey = Mapping->GetCurrentKey();
	return OutKey.IsValid() && OutKey != EKeys::AnyKey;
}

EOutlierLinkedBindingResult UOutlierInputUserSettings::ValidateLinkedBinding(
	const UOutlierInputBindingTable* Table, const FOutlierInputBindingTableRow& Row,
	FKey NewKey, FString& OutError) const
{
	OutError.Reset();
	TArray<FOutlierInputBindingTableRow> Members;
	if (!Table || !Table->ResolveLinkedRows(Row, Members, OutError)) return EOutlierLinkedBindingResult::InvalidData;
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	if (!Profile) return EOutlierLinkedBindingResult::Unavailable;

	// 1. 실제 IMC와 프로필 슬롯을 확인한 뒤 모든 구성원의 입력 제한을 검사한다.
	for (const auto& Member : Members)
	{
		const UInputMappingContext* Context = Member.MappingContext.Get();
		const FPlayerKeyMapping* PlayerMapping = Profile->FindKeyMapping(BindingArgs(Member));
		const bool bSourceExists = Context && Context->GetMappingsForProfile(Profile->GetProfileIdString()).ContainsByPredicate(
			[&Member](const FEnhancedActionKeyMapping& Mapping)
			{
				return Mapping.IsPlayerMappable() && Mapping.GetMappingName() == Member.MappingName
					&& Mapping.Action == Member.InputAction && Mapping.Key == Member.DefaultKey;
			});
		if (!bSourceExists || !PlayerMapping || PlayerMapping->GetDefaultKey() != Member.DefaultKey)
		{
			OutError = FString::Printf(TEXT("매핑/기본 키/슬롯 확인 실패: %s"), *Member.MappingName.ToString());
			return EOutlierLinkedBindingResult::InvalidData;
		}
		const EOutlierInputBindingValidation Validation = Member.ValidateReplacementKey(NewKey);
		if (Validation != EOutlierInputBindingValidation::Valid)
		{
			return Validation == EOutlierInputBindingValidation::AxisInputRequired
				? EOutlierLinkedBindingResult::AxisInputRequired : EOutlierLinkedBindingResult::InputRequired;
		}
	}

	// 2. 표시 여부와 관계없이 원본 행의 충돌 그룹을 검사한다. 비소유 보조 슬롯도 검사 대상이다.
	// 슈터 전용과 파트너 전용처럼 같은 역할에서 함께 쓰이지 않는 행은 같은 키를 써도 충돌이 아니다.
	for (const auto& Other : Table->GetAllRowsSorted())
	{
		if (!Members.ContainsByPredicate([&Other](const auto& Member)
		{
			return Member.ConflictGroup == Other.ConflictGroup && Member.SharesAnyRoleWith(Other);
		})) continue;
		for (const auto& Mapping : FindMappingsInRow(Other.MappingName))
		{
			if (!Mapping.GetHardwareDeviceId().HardwareDeviceIdentifier.IsNone() || Mapping.GetCurrentKey().IsGamepadKey()) continue;
			if (!OwnsProfileSlot(Members, Other.MappingName, Mapping.GetSlot()) && Mapping.GetCurrentKey() == NewKey)
			{
				OutError = FString::Printf(TEXT("다른 항목과 키 충돌: %s"), *Other.MappingName.ToString());
				return EOutlierLinkedBindingResult::Conflict;
			}
		}
	}

	// 3. 엔진의 ReservedByAction 규칙을 실제 매핑 단위로 검사한다.
	// 엔진 Query 결과는 Action까지만 알려 주므로 같은 Action의 비소유 매핑을 구분하기 위해
	// 원본 IMC와 프로필 슬롯을 함께 확인한다. 그룹 소유 슬롯만 예외이며 외부 예약은 유지한다.
	for (const UInputMappingContext* Context : GetRegisteredInputMappingContexts())
	{
		if (!Context) continue;
		for (const auto& Source : Context->GetMappingsForProfile(Profile->GetProfileIdString()))
		{
			if (!Source.Action || !Source.Action->bReserveAllMappings) continue;
			const FName Name = Source.GetMappingName();
			const auto& Mappings = FindMappingsInRow(Name);
			const bool bOwnedSource = Members.ContainsByPredicate([Context, &Source, Name](const auto& Member)
			{
				return Member.MappingContext.Get() == Context && Member.MappingName == Name
					&& Member.InputAction == Source.Action && Member.DefaultKey == Source.Key;
			});
			if (Source.IsPlayerMappable() && !Mappings.IsEmpty())
			{
				for (const auto& Mapping : Mappings)
				{
					if (!Mapping.GetHardwareDeviceId().HardwareDeviceIdentifier.IsNone()) continue;
					if (Mapping.GetCurrentKey() == NewKey
						&& !(bOwnedSource && OwnsProfileSlot(Members, Name, Mapping.GetSlot())))
					{
						OutError = TEXT("다른 Action이 예약한 키입니다.");
						return EOutlierLinkedBindingResult::Conflict;
					}
				}
			}
			else if (Source.Key == NewKey)
			{
				OutError = TEXT("재설정 불가능한 Action이 예약한 키입니다.");
				return EOutlierLinkedBindingResult::Conflict;
			}
		}
	}
	return EOutlierLinkedBindingResult::Success;
}

EOutlierLinkedBindingResult UOutlierInputUserSettings::ChangeLinkedBinding(
	const UOutlierInputBindingTable* Table, const FOutlierInputBindingTableRow& Row, FKey NewKey, FString& OutError)
{
	if (bChangingLinkedBinding) return EOutlierLinkedBindingResult::Unavailable;
	TGuardValue<bool> ChangeGuard(bChangingLinkedBinding, true);
	RegisterBindingContexts(Table);
	const EOutlierLinkedBindingResult Validation = ValidateLinkedBinding(Table, Row, NewKey, OutError);
	if (Validation != EOutlierLinkedBindingResult::Success) return Validation;
	TArray<FOutlierInputBindingTableRow> Members;
	if (!Table->ResolveLinkedRows(Row, Members, OutError)) return EOutlierLinkedBindingResult::InvalidData;
	UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	const FString ProfileId = Profile->GetProfileIdString();
	TMap<FName, FKeyMappingRow> PreviousRows;
	for (const auto& Member : Members) PreviousRows.FindOrAdd(Member.MappingName) = *Profile->FindKeyMappingRow(Member.MappingName);

	// 4. 전체 사전 검증 후 고유 저장 슬롯에만 적용한다. 실패하면 행 스냅샷으로
	// 키/override/dirty 상태와 보조 슬롯을 함께 복구하고 호출자는 저장하지 않는다.
	TArray<FOutlierInputBindingTableRow> Applied;
	bool bSucceeded = true;
	for (const auto& Member : Members)
	{
		if (OwnsProfileSlot(Applied, Member.MappingName, Member.Slot)) continue;
		FMapPlayerKeyArgs Args = BindingArgs(Member);
		Args.ProfileIdString = ProfileId;
		Args.NewKey = NewKey;
		Args.bDeferOnSettingsChangedBroadcast = true;
		FGameplayTagContainer Failure;
		if (NewKey == Members[0].DefaultKey) UnMapPlayerKey(Args, Failure);
		else MapPlayerKey(Args, Failure);
		const FPlayerKeyMapping* Mapping = Profile->FindKeyMapping(Args);
		if (!Failure.IsEmpty() || !Mapping || Mapping->GetCurrentKey() != NewKey || GetActiveKeyProfile() != Profile)
		{
			bSucceeded = false;
			break;
		}
		Applied.Add(Member);
	}
	// 복구 여부를 먼저 확정한 뒤 모든 행에 동일하게 적용한다.
	for (const auto& Previous : PreviousRows)
	{
		if (!Profile->FindKeyMappingRowMutable(Previous.Key)) bSucceeded = false;
	}
	for (auto& Previous : PreviousRows)
	{
		FKeyMappingRow* Current = Profile->FindKeyMappingRowMutable(Previous.Key);
		if (!Current) continue;
		if (!bSucceeded) *Current = Previous.Value;
		// FPlayerKeyMapping의 Hash에는 현재 키도 포함된다. 키 변경 뒤 Set을 재구성한다.
		TSet<FPlayerKeyMapping> Rehashed;
		for (const auto& Mapping : Current->Mappings) Rehashed.Add(Mapping);
		Current->Mappings = MoveTemp(Rehashed);
	}
	if (!bSucceeded)
	{
		OutError = TEXT("그룹 키 적용 실패: 이전 프로필 상태로 복구했습니다.");
		for (const auto& Member : Members)
		{
			// 전체 행을 복원했으므로 각 슬롯의 실제 키와 기본값/변경 상태를 다시 확인한다.
			const FPlayerKeyMapping* Restored = Profile->FindKeyMapping(BindingArgs(Member));
			const FPlayerKeyMapping* Expected = nullptr;
			for (const auto& Mapping : PreviousRows[Member.MappingName].Mappings)
			{
				if (Mapping.GetSlot() == Member.Slot && Mapping.GetHardwareDeviceId().HardwareDeviceIdentifier.IsNone()) Expected = &Mapping;
			}
			if (!ensureMsgf(Restored && Expected && Restored->GetCurrentKey() == Expected->GetCurrentKey()
				&& Restored->IsCustomized() == Expected->IsCustomized(), TEXT("Linked binding rollback failed")))
			{
				OutError = TEXT("그룹 키 적용 및 이전 상태 확인에 실패했습니다.");
			}
		}
		OnSettingsChanged.Broadcast(this);
		return EOutlierLinkedBindingResult::Failed;
	}
	return EOutlierLinkedBindingResult::Success;
}

bool UOutlierInputUserSettings::ChooseLinkedBindingKey(const UOutlierInputBindingTable* Table,
	const FOutlierInputBindingTableRow& Row, FKey& OutKey, bool& bOutNeedsSync, FString& OutError) const
{
	bOutNeedsSync = false;
	TArray<FOutlierInputBindingTableRow> Members;
	if (!Table || !Table->ResolveLinkedRows(Row, Members, OutError)) return false;
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	if (!Profile) return false;
	OutKey = Row.DefaultKey;
	const FPlayerKeyMapping* Primary = Profile->FindKeyMapping(BindingArgs(Row));
	if (!Primary) return false;
	if (Primary->IsCustomized()) OutKey = Primary->GetCurrentKey();
	else
	{
		// 대표 변경값 → 연결 대상들의 일치한 변경값 → 대표 기본값 순서로 선택한다.
		FKey LinkedKey;
		bool bDisagreement = false;
		for (const auto& Member : Members)
		{
			const FPlayerKeyMapping* Mapping = Profile->FindKeyMapping(BindingArgs(Member));
			if (!Mapping) return false;
			if (!Mapping->IsCustomized()) continue;
			if (LinkedKey.IsValid() && LinkedKey != Mapping->GetCurrentKey()) bDisagreement = true;
			LinkedKey = Mapping->GetCurrentKey();
		}
		if (!bDisagreement && LinkedKey.IsValid()) OutKey = LinkedKey;
	}
	for (const auto& Member : Members)
	{
		FKey Current;
		if (!GetBindingKey(Member, Current) || Current != OutKey) bOutNeedsSync = true;
	}
	return true;
}
