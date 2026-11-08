package codenamechain;

import codename.funkin.backend.system.Conductor;
import codename.funkin.game.Note;
import codename.funkin.game.PlayState;
import codename.funkin.game.Splash;
import codename.funkin.game.Strum;
import codename.funkin.game.StrumLine;
import codename.funkin.options.Options;
import flixel.FlxCamera;
import flixel.FlxG;
import flixel.FlxSprite;
import modchart.backend.standalone.IAdapter;

/** Modern CodeName data exposed to the shared FunkinModchart renderer. */
class CodeNameModchartAdapter implements IAdapter {
	public function new() {}

	private inline function state():PlayState
		return Std.isOfType(FlxG.state, PlayState) ? cast FlxG.state : null;

	public function onModchartingInitialization():Void {
		var current = state();
		if (current != null && current.splashHandler != null)
			current.splashHandler.visible = false;
	}

	public function getSongPosition():Float return Conductor.songPosition;
	public function getCurrentBeat():Float return Conductor.curBeatFloat;
	public function getCurrentCrochet():Float return Conductor.crochet;
	public function getBeatFromStep(step:Float):Float
		return Conductor.getTimeInBeats(Conductor.getStepsInTime(step, Conductor.curChangeIndex), Conductor.curChangeIndex);
	public function isTapNote(sprite:FlxSprite):Bool return Std.isOfType(sprite, Note);
	public function arrowHit(sprite:FlxSprite):Bool
		return Std.isOfType(sprite, Note) && cast(sprite, Note).wasGoodHit;
	public function isHoldEnd(sprite:FlxSprite):Bool
		return Std.isOfType(sprite, Note) && cast(sprite, Note).nextSustain == null;
	public function getHoldLength(sprite:FlxSprite):Float
		return Std.isOfType(sprite, Note) ? cast(sprite, Note).sustainLength : 0;
	public function getHoldParentTime(sprite:FlxSprite):Float {
		if (!Std.isOfType(sprite, Note)) return 0;
		var note:Note = cast sprite;
		return note.sustainParent != null ? note.sustainParent.strumTime : note.strumTime;
	}
	public function getTimeFromArrow(sprite:FlxSprite):Float
		return Std.isOfType(sprite, Note) ? cast(sprite, Note).strumTime : 0;

	public function getLaneFromArrow(sprite:FlxSprite):Int {
		if (Std.isOfType(sprite, Note)) return cast(sprite, Note).strumID;
		if (Std.isOfType(sprite, Strum)) return sprite.ID;
		if (Std.isOfType(sprite, Splash)) return cast(sprite, Splash).strumID;
		return 0;
	}

	public function getPlayerFromArrow(sprite:FlxSprite):Int {
		var line:StrumLine = null;
		if (Std.isOfType(sprite, Note)) line = cast(sprite, Note).strumLine;
		else if (Std.isOfType(sprite, Strum)) line = cast(sprite, Strum).strumLine;
		else if (Std.isOfType(sprite, Splash)) {
			var splash:Splash = cast sprite;
			if (splash.strum != null) line = splash.strum.strumLine;
		}
		var current = state();
		if (line == null || current == null || current.strumLines == null) return 0;
		var index = current.strumLines.members.indexOf(line);
		return index < 0 ? 0 : index;
	}

	private function getLine(player:Int):StrumLine {
		var current = state();
		if (current == null || current.strumLines == null || player < 0 || player >= current.strumLines.members.length)
			return null;
		return current.strumLines.members[player];
	}
	private function getReceptor(lane:Int, player:Int):Strum {
		var line = getLine(player);
		return line == null || lane < 0 || lane >= line.members.length ? null : line.members[lane];
	}
	public function getKeyCount(?player:Int = 0):Int {
		var line = getLine(player);
		return line == null ? 4 : line.members.length;
	}
	public function getPlayerCount():Int {
		var current = state();
		return current == null || current.strumLines == null ? 2 : current.strumLines.members.length;
	}
	public function getDefaultReceptorX(lane:Int, player:Int):Float {
		var receptor = getReceptor(lane, player);
		return receptor == null ? 0 : receptor.x;
	}
	public function getDefaultReceptorY(lane:Int, player:Int):Float {
		var receptor = getReceptor(lane, player);
		return receptor == null ? 0 : receptor.y;
	}
	public function getHoldSubdivisions(sprite:FlxSprite):Int
		#if MODCHARTING_FEATURES
		return Options.modchartingHoldSubdivisions < 1 ? 1 : Options.modchartingHoldSubdivisions;
		#else
		return 4;
		#end
	public function getDownscroll():Bool {
		var current = state();
		return current != null && current.downscroll;
	}
	public function getArrowCamera():Array<FlxCamera> {
		var current = state();
		return current == null || current.camHUD == null ? [] : [current.camHUD];
	}
	public function getCurrentScrollSpeed():Float {
		var current = state();
		return current == null ? 0 : current.scrollSpeed * 0.45;
	}

	public function getArrowItems():Array<Array<Array<FlxSprite>>> {
		var result:Array<Array<Array<FlxSprite>>> = [];
		var current = state();
		if (current == null || current.strumLines == null) return result;
		for (line in current.strumLines.members) {
			// Keep every player index present, including hidden or empty lines.
			var items:Array<Array<FlxSprite>> = [[], [], [], []];
			result.push(items);
			if (line == null || !line.exists || !line.visible) continue;
			for (strum in line.members)
				if (strum != null && strum.exists) items[0].push(strum);
			if (line.notes != null)
				line.notes.forEachAlive(note -> items[note.isSustainNote ? 2 : 1].push(note));
		}
		if (current.splashHandler != null)
			current.splashHandler.forEachAlive(splash -> {
				if (splash.strum == null || !splash.active) return;
				var player = getPlayerFromArrow(splash);
				var line = getLine(player);
				if (line != null && line.exists && line.visible && player < result.length)
					result[player][3].push(splash);
			});
		CodeNameModchartDiagnostics.captureItems(result);
		return result;
	}
}
