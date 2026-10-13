#include "UI/InputDisplayTypes.h"

#include "InputAction.h"
#include "Engine/Texture2D.h"

bool FInputDisplayDefinition::IsValid() const
{
	return !Expression.IsEmpty() && !Expression.ContainsByPredicate([](const FInputDisplayTerm& Term)
	{
		return !::IsValid(Term.InputAction)
			|| (Term.SeparatorBefore == EInputDisplaySeparator::Custom && !::IsValid(Term.SymbolTexture) && Term.SymbolText.IsEmpty());
	});
}
