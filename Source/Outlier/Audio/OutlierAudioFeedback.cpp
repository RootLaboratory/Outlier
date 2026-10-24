#include "Audio/OutlierAudioFeedback.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Outlier.h"
namespace OutlierAudioFeedback
{
static UOutlierAudioSubsystem* Audio(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>() : nullptr;
}
bool ShouldPlayShooterLowHP(bool bLocallyControlled, bool bDead, float Health)
{ return bLocallyControlled && !bDead && Health > 0.0f && Health < 40.0f; }
FGameplayTag Tag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), false); }
void Local(const UObject* Context, const TCHAR* AudioContext, const TCHAR* Type)
{
	if (UOutlierAudioSubsystem* Subsystem = Audio(Context))
	{
		const FString Name(AudioContext);
		if ((Name == TEXT("Audio.Context.UI.Menu.Click") || Name == TEXT("Audio.Context.UI.Menu.Back")) || Name.StartsWith(TEXT("Audio.Context.UI.Ingame.Upgrade.")))
			Subsystem->MarkMenuAction();
		FOutlierAudioPlayRequest Request;
		Request.EventTag = Tag(Type);
		const FGameplayTag ContextTag = Tag(AudioContext);
		if (!Request.EventTag.IsValid() || !ContextTag.IsValid())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Audio] Local request has an unregistered tag. Type='%s' Context='%s' Caller='%s'"),
				Type, AudioContext, *GetNameSafe(Context));
			return;
		}
		Request.ContextTags.AddTag(ContextTag);
		Subsystem->PlayLocal2D(Request);
	}
	else
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Audio] Local request has no audio subsystem. Caller='%s' Context='%s'"),
			*GetNameSafe(Context), AudioContext);
	}
}
void Owner(AActor* Emitter, const TCHAR* AudioContext, const TCHAR* Type)
{
	if (!IsValid(Emitter) || !Emitter->HasAuthority()) return;
	if (UOutlierAudioSubsystem* Subsystem = Audio(Emitter))
	{
		FOutlierAudioPlayRequest Request;
		Request.EventTag = Tag(Type);
		Request.ContextTags.AddTag(Tag(AudioContext));
		Request.EmitterActor = Emitter;
		Subsystem->PlayOwner2DFromServer(Request);
	}
}
void World(AActor* Emitter, const TCHAR* AudioContext, const TCHAR* Type, const FVector* Location)
{
	if (!IsValid(Emitter) || !Emitter->HasAuthority()) return;
	if (UOutlierAudioSubsystem* Subsystem = Audio(Emitter))
	{
		FOutlierAudioPlayRequest Request;
		Request.EventTag = Tag(Type);
		Request.ContextTags.AddTag(Tag(AudioContext));
		Request.EmitterActor = Emitter;
		Request.bHasLocation = true;
		Request.Location = Location ? *Location : Emitter->GetActorLocation();
		Subsystem->PlayRelevantAtLocationFromServer(Request);
	}
}
void Loop(AActor* Emitter, const TCHAR* AudioContext, bool bEnabled, const TCHAR* Type, bool bTwoD)
{
	if (UOutlierAudioSubsystem* Subsystem = Audio(Emitter))
		Subsystem->SetLocalEmitterLoop(Emitter, Tag(Type), Tag(AudioContext), bEnabled, bTwoD);
}
void StopLoops(AActor* Emitter)
{
	if (UOutlierAudioSubsystem* Subsystem = Audio(Emitter)) Subsystem->StopLocalEmitterLoops(Emitter);
}
}
