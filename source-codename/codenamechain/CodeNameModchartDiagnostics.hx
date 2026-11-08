package codenamechain;

import codename.funkin.backend.assets.ModsFolder;
import codename.funkin.backend.system.Conductor;
import codename.funkin.game.PlayState;
import flixel.FlxCamera;
import flixel.FlxG;
import flixel.FlxSprite;
import flixel.graphics.tile.FlxDrawTrianglesItem;
import haxe.Json;
import modchart.Manager;
import modchart.backend.graphics.ModchartRenderer.FMDrawInstruction;

/** Temporary, passive capture for the VMU Crisis renderer compatibility issue. */
@:access(flixel.FlxBasic)
@:access(flixel.FlxCamera)
class CodeNameModchartDiagnostics {
	static var owner:PlayState;
	static var bucket:Int = -1;
	static var enabled:Bool = false;
	static var capturedKinds:Array<String> = [];
	static inline var PATH = "crash/vmu-crisis-render.jsonl";

	public static function captureItems(items:Array<Array<Array<FlxSprite>>>):Void {
		enabled = false;
		#if sys
		if (!Std.isOfType(FlxG.state, PlayState) || PlayState.SONG == null || PlayState.SONG.meta == null
			|| PlayState.SONG.meta.name.toLowerCase() != "crisis" || ModsFolder.currentModFolder != "VMU") return;
		var state:PlayState = cast FlxG.state;
		if (owner != state) {
			owner = state;
			bucket = -1;
			write({kind: "session", build: "inactive-modifiers-fix-2", difficulty: PlayState.difficulty});
		}
		var currentBucket = Std.int(Conductor.songPosition / 5000);
		if (Conductor.songPosition < 0 || currentBucket == bucket) return;
		bucket = currentBucket;
		enabled = true;
		capturedKinds = [];
		var lines:Array<Dynamic> = [];
		for (index in 0...items.length) {
			var line = state.strumLines.members[index];
			lines.push({player: index, visible: line == null ? false : line.visible,
				counts: [for (group in items[index]) group.length],
				receptors: [for (sprite in items[index][0]) describe(sprite)]});
		}
		var manager = Manager.instance;
		write({kind: "items", time: Conductor.songPosition, beat: Conductor.curBeatFloat,
			manager: manager == null ? null : {visible: manager.visible, exists: manager.exists,
				active: manager.active, stateIndex: state.members.indexOf(manager)},
			renderTile: FlxG.renderTile, renderBlit: FlxG.renderBlit,
			hud: describeCamera(state.camHUD), lines: lines});
		#end
	}

	public static function captureMesh(instruction:FMDrawInstruction, camera:FlxCamera):Void {
		if (!enabled || instruction == null || camera == null) return;
		var sprite = instruction.item;
		var adapter = modchart.backend.standalone.Adapter.instance;
		if (adapter.getPlayerFromArrow(sprite) != 1) return;
		var kind = adapter.isTapNote(sprite) ? "note" : "receptor";
		if (capturedKinds.indexOf(kind) >= 0) return;
		capturedKinds.push(kind);
		var batch:FlxDrawTrianglesItem = Std.isOfType(camera._currentDrawItem, FlxDrawTrianglesItem)
			? cast camera._currentDrawItem : null;
		write({kind: "mesh", spriteKind: kind, time: Conductor.songPosition, sprite: describe(sprite),
			vertices: [for (value in instruction.vertices) Std.string(value)],
			uvt: [for (value in instruction.uvt) Std.string(value)],
			indices: [for (value in instruction.indices) value],
			alpha: instruction.colorData[0].alphaMultiplier, camera: describeCamera(camera),
			batch: batch == null ? null : {vertices: batch.vertices.length, indices: batch.indices.length,
				uvt: batch.uvtData.length, triangles: batch.numTriangles}});
	}

	static function describe(sprite:FlxSprite):Dynamic {
		return {className: Type.getClassName(Type.getClass(sprite)), lane: sprite.ID,
			visible: sprite.visible, fmVisible: sprite._fmVisible, exists: sprite.exists, alpha: sprite.alpha,
			x: Std.string(sprite.x), y: Std.string(sprite.y), scaleX: Std.string(sprite.scale.x), scaleY: Std.string(sprite.scale.y),
			graphic: sprite.graphic == null ? null : sprite.graphic.key,
			frameGraphic: sprite.frame == null || sprite.frame.parent == null ? null : sprite.frame.parent.key,
			frameWidth: sprite.frameWidth, frameHeight: sprite.frameHeight,
			shader: sprite.shader == null ? null : Type.getClassName(Type.getClass(sprite.shader)),
			cameras: sprite._cameras == null ? null : [for (camera in sprite._cameras) describeCamera(camera)]};
	}

	static function describeCamera(camera:FlxCamera):Dynamic {
		return camera == null ? null : {id: camera.ID, registered: FlxG.cameras.list.indexOf(camera) >= 0,
			exists: camera.exists, visible: camera.visible, alpha: camera.alpha, zoom: camera.zoom,
			width: camera.width, height: camera.height, canvasAlpha: camera.canvas.alpha,
			canvasVisible: camera.canvas.visible, flashAlpha: camera.flashSprite.alpha};
	}

	static function write(record:Dynamic):Void {
		#if sys
		try {
			if (!sys.FileSystem.exists("crash")) sys.FileSystem.createDirectory("crash");
			var output = sys.io.File.append(PATH, false);
			try output.writeString(Json.stringify(record) + "\n") catch (error:Dynamic) {
				output.close();
				throw error;
			}
			output.close();
		} catch (error:Dynamic) {
			// Diagnostics must never interrupt the modchart or the song.
		}
		#end
	}
}
