package codenamechain;

import codename.funkin.backend.assets.ModsFolder;
import codename.funkin.backend.assets.Paths;
import codename.funkin.backend.system.Conductor;
import codename.funkin.game.PlayState;
import flixel.FlxBasic;
import flixel.FlxG;
import flixel.FlxSprite;
import flixel.group.FlxGroup.FlxTypedGroup;
import flixel.group.FlxSpriteGroup.FlxTypedSpriteGroup;
import haxe.ds.ObjectMap;

/** Captured only on errors, before the error screen changes the scene. */
class CodeNameCrashContext
{
	@:access(flixel.FlxSprite)
	public static function capture():String
	{
		if (!CodeNameMode.active) return "";
		var lines = [
			'[codename_context]',
			'mod=${ModsFolder.currentModFolder}',
			'state=${Type.getClassName(Type.getClass(FlxG.state))}'
		];
		if (Std.isOfType(FlxG.state, PlayState))
		{
			lines.push('song=${PlayState.SONG == null || PlayState.SONG.meta == null ? "unknown" : PlayState.SONG.meta.name}');
			lines.push('difficulty=${PlayState.difficulty}');
			lines.push('song_position_ms=${Conductor.songPosition}');
			lines.push('step=${Conductor.curStep}');
		}

		var visited = new ObjectMap<FlxBasic, Bool>();
		var invalidCount = 0;
		function visit(object:FlxBasic, path:String):Void
		{
			if (object == null || visited.exists(object)) return;
			visited.set(object, true);
			if (Std.isOfType(object, FlxSprite))
			{
				var sprite:FlxSprite = cast object;
				var drawnGraphic = sprite._frame == null ? null : sprite._frame.parent;
				if ((sprite.graphic != null && sprite.graphic.isDestroyed)
					|| (drawnGraphic != null && drawnGraphic.isDestroyed))
				{
					invalidCount++;
					if (invalidCount <= 64)
					{
						var paths:Array<String> = [];
						for (key => frames in Paths.tempFramesCache)
							if (frames != null && (frames == sprite.frames || frames.parent == drawnGraphic))
								paths.push(key);
						lines.push('invalid_sprite=$path class=${Type.getClassName(Type.getClass(sprite))}'
							+ ' exists=${sprite.exists} visible=${sprite.visible} alpha=${sprite.alpha}'
							+ ' graphic_key=${sprite.graphic == null ? "null" : sprite.graphic.key}'
							+ ' frame_graphic_destroyed=${drawnGraphic != null && drawnGraphic.isDestroyed}'
							+ ' cached_frame_paths=${paths.join(",")}');
					}
				}
			}
			var members:Array<FlxBasic> = null;
			if (Std.isOfType(object, FlxTypedSpriteGroup))
			{
				var group:FlxTypedSpriteGroup<FlxSprite> = cast object;
				members = cast group.group.members;
			}
			else if (Std.isOfType(object, FlxTypedGroup))
			{
				var group:FlxTypedGroup<FlxBasic> = cast object;
				members = group.members;
			}
			if (members != null)
				for (index => member in members) visit(member, '$path.members[$index]');
		}
		visit(FlxG.state, "state");
		lines.push('invalid_sprite_count=$invalidCount');
		return lines.join("\n");
	}
}
