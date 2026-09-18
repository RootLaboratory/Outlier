#include "Enemy/EnemyPoolDefinition.h"

#if WITH_EDITOR
#include "Enemy/EnemyBase.h"
#include "Misc/DataValidation.h"

EDataValidationResult UEnemyPoolDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FSoftObjectPath> SeenClasses;

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEnemyPoolEntry& Entry = Entries[Index];
		const FSoftObjectPath ClassPath = Entry.EnemyClass.ToSoftObjectPath();
		if (ClassPath.IsNull())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyPool", "MissingEnemyClass", "Entries[{0}] EnemyClass is empty."),
				Index));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenClasses.Contains(ClassPath))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyPool", "DuplicateEnemyClass", "Entries[{0}] duplicates EnemyClass {1}."),
				Index,
				FText::FromString(ClassPath.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			SeenClasses.Add(ClassPath);
		}

		if (Entry.PrewarmCount < 0)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyPool", "NegativePrewarm", "Entries[{0}] PrewarmCount must be at least zero."),
				Index));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.MaxCount < 1)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyPool", "InvalidMax", "Entries[{0}] MaxCount must be at least one."),
				Index));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.PrewarmCount > Entry.MaxCount)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("EnemyPool", "PrewarmExceedsMax", "Entries[{0}] PrewarmCount cannot exceed MaxCount."),
				Index));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
