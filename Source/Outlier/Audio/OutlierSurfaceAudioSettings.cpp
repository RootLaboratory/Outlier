#include "Audio/OutlierSurfaceAudioSettings.h"

void FOutlierSurfaceContextMapping::GetCandidates(EPhysicalSurface Surface, TArray<FGameplayTag>& OutContexts) const
{
	OutContexts.Reset();
	const FGameplayTag SurfaceContext = Surface == SurfaceType1 ? NoneMetalContext
		: Surface == SurfaceType2 ? MetalContext
		: Surface == SurfaceType3 ? GlassContext : DefaultContext;
	if (SurfaceContext.IsValid()) OutContexts.Add(SurfaceContext);
	if (DefaultContext.IsValid()) OutContexts.AddUnique(DefaultContext);
}

void UOutlierSurfaceAudioSettings::BuildMappings(TMap<FGameplayTag, FOutlierSurfaceContextMapping>& OutMappings) const
{
	OutMappings.Reset();
	auto Tag = [](const TCHAR* Name)
	{
		return Name ? FGameplayTag::RequestGameplayTag(FName(Name), false) : FGameplayTag();
	};
	auto Add = [&](const TCHAR* Base, const TCHAR* Default, const TCHAR* NoneMetal, const TCHAR* Metal, const TCHAR* Glass)
	{
		const FGameplayTag BaseTag = Tag(Base);
		if (!BaseTag.IsValid()) return;
		FOutlierSurfaceContextMapping Mapping;
		Mapping.DefaultContext = Tag(Default);
		Mapping.NoneMetalContext = Tag(NoneMetal);
		Mapping.MetalContext = Tag(Metal);
		Mapping.GlassContext = Tag(Glass);
		OutMappings.Add(BaseTag, Mapping);
		// Notify에 Plastic을 지정해도 기준 이벤트와 같은 표면 매핑을 사용한다.
		if (Mapping.DefaultContext.IsValid()) OutMappings.Add(Mapping.DefaultContext, Mapping);
	};
	// Surface Type은 유지한다. NoneMetal/미지정 표면은 Plastic, Metal과 Glass는 해당 변형을 사용한다.
	// Marble 태그는 등록만 하며 현재 Surface Type에 임의로 연결하지 않는다.
	Add(TEXT("Audio.Context.Player.01.Type.Rifle.Hit"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Plastic"), nullptr, TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Metal"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Glass"));
	Add(TEXT("Audio.Context.Player.01.Type.Pistol.Hit"), TEXT("Audio.Context.Player.01.Type.Pistol.Hit.Plastic"), nullptr, nullptr, nullptr);
	Add(TEXT("Audio.Context.Player.01.Type.Melee.Hit"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Plastic"), nullptr, TEXT("Audio.Context.Player.01.Type.Melee.Hit.Metal"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Glass"));
	Add(TEXT("Audio.Context.Player.01.Movement.Footstep"), TEXT("Audio.Context.Player.01.Movement.Footstep.Plastic"), nullptr, TEXT("Audio.Context.Player.01.Movement.Footstep.Metal"), nullptr);
	Add(TEXT("Audio.Context.Player.01.Movement.Jump"), TEXT("Audio.Context.Player.01.Movement.Jump.Plastic"), nullptr, TEXT("Audio.Context.Player.01.Movement.Jump.Metal"), nullptr);
	Add(TEXT("Audio.Context.Player.01.Movement.Land"), TEXT("Audio.Context.Player.01.Movement.Land.Plastic"), nullptr, TEXT("Audio.Context.Player.01.Movement.Land.Metal"), nullptr);
	for (const auto& Pair : ContextMappings) OutMappings.Add(Pair.Key, Pair.Value);
}
