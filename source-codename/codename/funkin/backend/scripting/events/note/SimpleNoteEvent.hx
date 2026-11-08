package codename.funkin.backend.scripting.events.note;

import codename.funkin.game.Note;

final class SimpleNoteEvent extends CancellableEvent {
	/**
		Note that is affected.
	**/
	public var note:Note;
}