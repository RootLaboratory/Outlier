#include "ProceduralAnimValuesExporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Shooter/Anim/ProceduralAnimValues.h"
#include "UObject/UnrealType.h"

namespace
{
	FString EscapeCsv(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return TEXT("\"") + Escaped + TEXT("\"");
	}

	void CollectFields(const UStruct* Struct, const void* Data, const FString& Prefix, TMap<FString, FString>& Values)
	{
		for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const FString Path = Prefix + TEXT(".") + Property->GetName();
			const void* Value = Property->ContainerPtrToValuePtr<void>(Data);

			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CollectFields(StructProperty->Struct, Value, Path, Values);
				continue;
			}

			FString TextValue;
			Property->ExportTextItem_Direct(TextValue, Value, nullptr, nullptr, PPF_None);
			Values.Add(Path, MoveTemp(TextValue));
		}
	}
}

bool OutlierEditor::ExportProceduralAnimValues(FString& OutPath, FString& OutError, int32& OutAssetCount)
{
	OutPath.Reset();
	OutError.Reset();
	OutAssetCount = 0;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UProceduralAnimValues::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TArray<FString> AssetPaths;
	TMap<FString, TMap<FString, FString>> ValuesByAsset;
	TSet<FString> FieldPaths;
	for (const FAssetData& AssetData : AssetDataList)
	{
		const UProceduralAnimValues* Asset = Cast<UProceduralAnimValues>(AssetData.GetAsset());
		if (!Asset)
		{
			continue;
		}

		const FString AssetPath = Asset->GetPathName();
		TMap<FString, FString>& Values = ValuesByAsset.FindOrAdd(AssetPath);
		CollectFields(FWeaponValues::StaticStruct(), &Asset->WeaponValues, TEXT("WeaponValues"), Values);
		CollectFields(FRecoilValues::StaticStruct(), &Asset->RecoilValues, TEXT("RecoilValues"), Values);
		for (const TPair<FString, FString>& Field : Values)
		{
			FieldPaths.Add(Field.Key);
		}
		AssetPaths.Add(AssetPath);
	}

	if (AssetPaths.IsEmpty())
	{
		OutError = TEXT("No ProceduralAnimValues Data Assets were found under /Game.");
		return false;
	}

	AssetPaths.Sort();
	TArray<FString> SortedFields;
	SortedFields.Reserve(FieldPaths.Num());
	for (const FString& FieldPath : FieldPaths)
	{
		SortedFields.Add(FieldPath);
	}
	SortedFields.Sort();

	FString Csv = TEXT("Property");
	for (const FString& AssetPath : AssetPaths)
	{
		Csv += TEXT(",") + EscapeCsv(AssetPath);
	}
	Csv += TEXT("\r\n");

	for (const FString& FieldPath : SortedFields)
	{
		Csv += EscapeCsv(FieldPath);
		for (const FString& AssetPath : AssetPaths)
		{
			const FString* Value = ValuesByAsset.FindChecked(AssetPath).Find(FieldPath);
			Csv += TEXT(",") + EscapeCsv(Value ? *Value : FString());
		}
		Csv += TEXT("\r\n");
	}

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ProceduralAnimExports"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Could not create export directory: %s"), *Directory);
		return false;
	}

	const FDateTime Now = FDateTime::Now();
	OutPath = FPaths::Combine(
		Directory,
		FString::Printf(TEXT("ProceduralAnimValues_%s_%03d.csv"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")), Now.GetMillisecond()));
	if (!FFileHelper::SaveStringToFile(Csv, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		OutError = FString::Printf(TEXT("Could not write CSV: %s"), *OutPath);
		OutPath.Reset();
		return false;
	}

	OutAssetCount = AssetPaths.Num();
	return true;
}
