package codename.funkin.backend.scripting.events.dialogue;

import codename.funkin.game.cutscenes.dialogue.DialogueCharacter;

final class DialogueBoxCharPopupEvent extends CancellableEvent
{
	public var char:DialogueCharacter;

	public var force:Bool;
}