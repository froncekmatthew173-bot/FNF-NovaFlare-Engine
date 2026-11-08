package games.objects;

/*
 * NovaFlare Engine * Copyright (c) 2023-2026 NF Crew
 * https://github.com/NovaFlare-Engine-Concentration/FNF-NovaFlare-Engine/
*/

import openfl.display.Bitmap;
import openfl.display.BitmapData;
import openfl.display.Shape;
import openfl.utils.Assets;

import flixel.graphics.FlxGraphic;
import flixel.util.FlxSave;

import general.backend.InputFormatter;

import options.OptionsHelpers;

import games.backend.Song;

class KeyboardViewer extends FlxSpriteGroup
{
	public var isPreview:Bool = false;
	var autoPressLoop:Bool = false;
	var autoPressIndex:Int = 0;
	var autoPressHoldTime:Float = 0.32;
	var autoPressStopTime:Float = 0.05;
	var autoPressRunning:Bool = false;
	var previewClock:Float = 0;

	var destroyed:Bool = false;

	public var currentBGColor:FlxColor = {
		var c:Null<FlxColor> = OptionsHelpers.colorArray(ClientPrefs.data.keyboardBGColor);
		c != null ? c : FlxColor.WHITE;
	};

	public var currentTextColor:FlxColor = {
		var c:Null<FlxColor> = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
		c != null ? c : FlxColor.BLACK;
	};

	public var noteArrays:Array<Array<TimeDis>> = []; // 存储所有键位的数组
	public var keyAlphas:Array<KeyButtonAlpha> = []; // 存储键位透明度对象
	public var keyTexts:Array<FlxText> = []; // 存储键位文本对象

	public var _x:Float;
	public var _y:Float;
	public var _width:Float;
	public var _height:Float;
	public var kpsText:FlxText;
	public var totalText:FlxText;

	public var keys:Int = 4; // 默认键位数

	var total:Int = 0;

	public static var instance:KeyboardViewer;

	public function new(X:Float, Y:Float, preview:Bool = false)
	{
		super();
		instance = this;
		moves = false;

		isPreview = preview;

		_x = X;
		_y = Y;

		var mania:Int = 3;
		if(PlayState.SONG != null) mania = PlayState.SONG.mania;

		if (preview)
		{
			mania = 3;
			autoPressLoop = true;
		}

		keys = mania + 1;

		for(i in 0...keys) noteArrays.push([]);

		_width = (KeyButton.size + 4) * keys;
		_height = (KeyButton.size + 4) * 2;

		if (mania == 3)
		{
			for (i in 0...4)
			{
				var obj:KeyButton = new KeyButton(X + (KeyButton.size + 4) * i, Y, KeyButton.size, KeyButton.size);
				add(obj);
			}
			for (i in 0...4)
			{
				var obj:KeyButtonAlpha = new KeyButtonAlpha(X + (KeyButton.size + 4) * i, Y);
				keyAlphas.push(obj);
				add(obj);
			}
			var textArray:Array<String> = createArray();
			for (i in 0...4)
			{
				var obj:FlxText = new FlxText(X + (KeyButton.size + 4) * i + keyAlphas[i].width / 2, Y + keyAlphas[i].height / 2, 50, textArray[i], 10, false);
				obj.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 20, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
				obj.x -= obj.width / 2;
				obj.y -= obj.height / 2;
				obj.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
				obj.alpha = ClientPrefs.data.keyboardAlpha;
				obj.moves = false;
				keyTexts.push(obj);
				add(obj);
			}
			for (i in 0...2)
			{
				var obj:KeyButton = new KeyButton(X + (KeyButton.size + 4) * i * 2, Y + KeyButton.size + 4, KeyButton.size * 2 + 4, KeyButton.size);
				add(obj);
			}
			var textArray:Array<String> = ['KPS', 'total'];
			for (i in 0...2)
			{
				var obj:FlxText = new FlxText(members[12 + i].x + members[12 + i].width / 2, members[12 + i].y + members[12 + i].height / 4,
					KeyButton.size * 2 + 4, textArray[i], 20, false);
				obj.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 25, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
				obj.x -= obj.width / 2;
				obj.y -= obj.height / 2;
				obj.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
				obj.alpha = ClientPrefs.data.keyboardAlpha;
				obj.antialiasing = ClientPrefs.data.antialiasing;
				obj.moves = false;
				add(obj);
			}
			kpsText = new FlxText(members[12].x + members[12].width / 2, members[12].y + members[12].height / 5 * 3.5, KeyButton.size * 2 + 4, '0', 15, false);
			kpsText.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 15, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
			kpsText.x -= kpsText.width / 2;
			kpsText.y -= kpsText.height / 2;
			kpsText.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
			kpsText.alpha = ClientPrefs.data.keyboardAlpha;
			kpsText.moves = false;

			if (FlxG.save.data.keyboardtotal != null)
				total = FlxG.save.data.keyboardtotal;
			totalText = new FlxText(members[13].x + members[13].width / 2, members[13].y + members[13].height / 5 * 3.5, KeyButton.size * 2 + 4,
				Std.string(total), 15, false);
			totalText.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 15, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
			totalText.x -= totalText.width / 2;
			totalText.y -= totalText.height / 2;
			totalText.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
			totalText.alpha = ClientPrefs.data.keyboardAlpha;
			totalText.moves = false;
			add(kpsText);
			add(totalText);
		}
		else // 多键模式
		{
			for (i in 0...keys)
			{
				var obj:KeyButton = new KeyButton(X + (KeyButton.size + 4) * i, Y, KeyButton.size, KeyButton.size);
				add(obj);
			}

			for (i in 0...keys)
			{
				var obj:KeyButtonAlpha = new KeyButtonAlpha(X + (KeyButton.size + 4) * i, Y);
				keyAlphas.push(obj);
				add(obj);
			}

			var textArray:Array<String> = createArray();
			for (i in 0...keys)
			{
				var obj:FlxText = new FlxText(X + (KeyButton.size + 4) * i + keyAlphas[i].width / 2, Y + keyAlphas[i].height / 2, 50, textArray[i], 10, false);
				obj.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 20, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
				obj.x -= obj.width / 2;
				obj.y -= obj.height / 2;
				obj.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
				obj.alpha = ClientPrefs.data.keyboardAlpha;
				obj.moves = false;
				keyTexts.push(obj);
				add(obj);
			}

			var bigButtonWidth:Int = Std.int((KeyButton.size * 2 + 4) * (keys / 4));
			var startX = X + (_width - bigButtonWidth * 2 - 4) / 2;

			for (i in 0...2)
			{
				var obj:KeyButton = new KeyButton(startX + (bigButtonWidth + 4) * i, Y + KeyButton.size + 4, bigButtonWidth, KeyButton.size);
				add(obj);
			}
			var textArray:Array<String> = ['KPS', 'total'];
			for (i in 0...2)
			{
				var obj:FlxText = new FlxText(startX + (bigButtonWidth + 4) * i + bigButtonWidth / 2, Y + KeyButton.size + 4 + KeyButton.size / 4,
					bigButtonWidth, textArray[i], 20, false);
				obj.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 25, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
				obj.x -= obj.width / 2;
				obj.y -= obj.height / 2;
				obj.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
				obj.alpha = ClientPrefs.data.keyboardAlpha;
				obj.antialiasing = ClientPrefs.data.antialiasing;
				obj.moves = false;
				add(obj);
			}

			kpsText = new FlxText(startX + bigButtonWidth / 2, Y + KeyButton.size + 4 + KeyButton.size / 5 * 3.5, bigButtonWidth, '0', 15, false);
			kpsText.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 15, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
			kpsText.x -= kpsText.width / 2;
			kpsText.y -= kpsText.height / 2;
			kpsText.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
			kpsText.alpha = ClientPrefs.data.keyboardAlpha;
			kpsText.moves = false;

			if (FlxG.save.data.keyboardtotal != null)
				total = FlxG.save.data.keyboardtotal;
			totalText = new FlxText(startX + bigButtonWidth + 4 + bigButtonWidth / 2, Y + KeyButton.size + 4 + KeyButton.size / 5 * 3.5, bigButtonWidth,
				Std.string(total), 15, false);
			totalText.setFormat(Assets.getFont("assets/fonts/montserrat.ttf").fontName, 15, FlxColor.WHITE, CENTER, FlxTextBorderStyle.OUTLINE, 0x00);
			totalText.x -= totalText.width / 2;
			totalText.y -= totalText.height / 2;
			totalText.color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardTextColor);
			totalText.alpha = ClientPrefs.data.keyboardAlpha;
			totalText.moves = false;
			add(kpsText);
			add(totalText);
		}

		DisBitmap.addCache();

	}

	public function setBGColor(color:FlxColor):Void
	{
		currentBGColor = color;
		var i:Int = members.length - 1;
		while (i >= 0)
		{
			var obj = members[i];
			if (obj != null)
			{
				if (Std.isOfType(obj, KeyButton))
					(cast obj:KeyButton).color = color;
				else if (Std.isOfType(obj, TimeDis))
					(cast obj:TimeDis).color = color;
			}
			i--;
		}
	}

	public function setTextColor(color:FlxColor):Void
	{
		currentTextColor = color;
		var i:Int = members.length - 1;
		while (i >= 0)
		{
			var obj = members[i];
			if (obj != null && Std.isOfType(obj, FlxText))
				(cast obj:FlxText).color = color;
			i--;
		}
	}

	function loopAutoPress():Void
	{
		autoPressRunning = true;

		FlxTimer.wait(autoPressStopTime, () ->
		{
			pressed(autoPressIndex);

			FlxTimer.wait(autoPressHoldTime, () ->
			{
				released(autoPressIndex);
				autoPressIndex = (autoPressIndex + 1) % keyAlphas.length;
				autoPressRunning = false;
			});
		});
	}

	public function pressed(key:Int)
	{
		if (destroyed) return;

		if(key < keyAlphas.length && keyAlphas[key] != null) {
			keyAlphas[key].alpha = 1 * ClientPrefs.data.keyboardAlpha;
		}
		if(key < keyTexts.length && keyTexts[key] != null) {
			keyTexts[key].color = FlxColor.BLACK;
		}

		if (!isPreview)
		{
			if (!PlayState.replayMode)
				total++;
			totalText.text = Std.string(total);
			hitArray.unshift(Date.now());
		}

		if (!ClientPrefs.data.keyboardTimeDisplay)
			return;

		var startT:Float = isPreview ? previewClock : Conductor.songPosition;
		var obj:TimeDis = new TimeDis(key, startT, _x, _y);
		if (isPreview)
			obj.color = currentBGColor;
		if (isPreview)
		{
			obj.previewMode = true;
			obj.previewTime = startT;
		}
		add(obj);

		if(key < noteArrays.length) {
			var arr = noteArrays[key];
			if(arr.length > 0 && arr[arr.length - 1].endTime == -999999)
				arr[arr.length - 1].endTime = startT;
			arr.push(obj);
		}
	}

	public function released(key:Int)
	{
		if (destroyed) return;

		if(key < keyAlphas.length && keyAlphas[key] != null)
			keyAlphas[key].alpha = 0;
		if(key < keyTexts.length && keyTexts[key] != null)
			keyTexts[key].color = currentTextColor;

		if(key < noteArrays.length) {
			var arr = noteArrays[key];
			if(arr.length > 0 && arr[arr.length - 1].endTime == -999999)
				arr[arr.length - 1].endTime = isPreview ? previewClock : Conductor.songPosition;
		}
	}

	public function save()
	{
		FlxG.save.data.keyboardtotal = total;
		FlxG.save.flush();
	}

	public function createArray():Array<String>
	{
		var array:Array<String> = [];
		var mania:Int = (PlayState.SONG != null) ? PlayState.SONG.mania : 3;
		var keys:Int = mania + 1;

		if (mania == 3)
		{
			array.push(InputFormatter.getKeyName(Controls.instance.keyboardBinds['note_left'][0]));
			array.push(InputFormatter.getKeyName(Controls.instance.keyboardBinds['note_down'][0]));
			array.push(InputFormatter.getKeyName(Controls.instance.keyboardBinds['note_up'][0]));
			array.push(InputFormatter.getKeyName(Controls.instance.keyboardBinds['note_right'][0]));
			return array;
		}

		var keybindID = '${mania}_key';

		for (i in 0...keys)
		{
			// Get the keybind name using the format used in ClientPrefs
			var bindName = '${keybindID}_${i}';
			var keysArray = Controls.instance.keyboardBinds.get(bindName);

			if(keysArray != null && keysArray.length > 0)
				array.push(InputFormatter.getKeyName(keysArray[0]));
			else
				array.push('?');
		}
		return array;
	}

	public function removeObj(obj:TimeDis)
	{
		if(obj.line < noteArrays.length) {
			noteArrays[obj.line].remove(obj);
		}
		remove(obj, true);
		obj.destroy();
	}

	public var kps:Int = 0;
	public var kpsCheck:Int = 0;
	public var hitArray:Array<Date> = [];

	override function update(elapsed:Float)
	{
		if (isPreview)
		{
			super.update(elapsed);
			previewClock += elapsed * 1000;

			if (autoPressLoop && !autoPressRunning && keyAlphas.length > 0)
				loopAutoPress();

			return;
		}

		super.update(elapsed);
		var i = hitArray.length - 1;
		while (i >= 0)
		{
			var time:Date = hitArray[i];
			if (time != null && time.getTime() + 1000 < Date.now().getTime())
				hitArray.remove(time);
			else
				i = -1;
			i--;
		}
		kps = hitArray.length;

		if (kpsCheck != kps)
		{
			kpsCheck = kps;
			kpsText.text = Std.string(kps);
		}
	}

	override function destroy()
	{
		destroyed = true;
		autoPressLoop = false;
		autoPressRunning = false;
		super.destroy();
	}
}

class KeyButton extends FlxSprite
{
	var bgAlpha = 0.3 * ClientPrefs.data.keyboardAlpha;
	var lineAlpha = 0.8 * ClientPrefs.data.keyboardAlpha;

	public static var size = 50;

	public function new(X:Float, Y:Float, Width:Int, Height:Int)
	{
		super(X, Y);

		var shape:Shape = new Shape();
		shape.graphics.lineStyle(2, FlxColor.WHITE, lineAlpha);
		shape.graphics.drawRoundRect(0, 0, Width, Height, Std.int(size / 3), Std.int(size / 3));
		shape.graphics.lineStyle();
		shape.graphics.beginFill(FlxColor.WHITE, bgAlpha);
		shape.graphics.drawRoundRect(0, 0, Width, Height, Std.int(size / 3), Std.int(size / 3));
		shape.graphics.endFill();

		var BitmapData:BitmapData = new BitmapData(Width, Height, 0x00FFFFFF);
		BitmapData.draw(shape);

		loadGraphic(BitmapData);
		antialiasing = ClientPrefs.data.antialiasing;
		color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardBGColor);
		moves = false;
	}
}

class KeyButtonAlpha extends FlxSprite
{
	var size = KeyButton.size;

	public var tween = FlxTween;

	public function new(X:Float, Y:Float)
	{
		super(X, Y);

		var shape:Shape = new Shape();
		shape.graphics.beginFill(FlxColor.WHITE, 1);
		shape.graphics.drawRoundRect(0, 0, size, size, Std.int(size / 3), Std.int(size / 3));
		shape.graphics.endFill();

		var BitmapData:BitmapData = new BitmapData(size, size, 0x00FFFFFF);
		BitmapData.draw(shape);

		loadGraphic(BitmapData);
		antialiasing = ClientPrefs.data.antialiasing;
		alpha = 0;
		moves = false;
	}
}

class TimeDis extends FlxSprite
{
	public var startTime:Float;
	public var endTime:Float = -999999;
	public var line:Int;
	public var previewMode:Bool = false;
	public var previewTime:Float = 0;

	var durationTime:Float = ClientPrefs.data.keyboardTime;

	public function new(Line:Int, Time:Float, X:Float, Y:Float)
	{
		this.line = Line;
		super(X + Line * (KeyButton.size + 4), Y - 4 - DisBitmap.Height);
		this.startTime = Time;
		frames = Cache.getFrame('keyboardViewer');
		_frame.frame.height = 1;
		color = OptionsHelpers.colorArray(ClientPrefs.data.keyboardBGColor);
		alpha = ClientPrefs.data.keyboardAlpha;
		moves = false;
	}

	var saveTime:Float;

	override function update(elapsed:Float)
	{
		var songPos:Float = previewMode ? previewTime : Conductor.songPosition;

		if (endTime == -999999)
		{
			_frame.frame.y = (1 - ((songPos - startTime) / durationTime)) * DisBitmap.Height;
			_frame.frame.height = ((songPos - startTime) / durationTime) * DisBitmap.Height;
			offset.y = -(1 - ((songPos - startTime) / durationTime)) * DisBitmap.Height;
			if (_frame.frame.y < 0)
				_frame.frame.y = 0;
			if (songPos - startTime > durationTime)
				offset.y = 0;
			saveTime = songPos;
		}
		else
		{
			if (endTime - startTime < durationTime)
				_frame.frame.y = (1 - ((songPos - startTime) / durationTime)) * DisBitmap.Height;
			else
				_frame.frame.y = (1 - ((songPos - (endTime - durationTime)) / durationTime)) * DisBitmap.Height;
			offset.y -= -((songPos - saveTime) / durationTime) * DisBitmap.Height;
			saveTime = songPos;
		}
		if (_frame.frame.height > DisBitmap.Height)
			_frame.frame.height = DisBitmap.Height;
		if (_frame.frame.height <= 0)
			_frame.frame.height = 1; // fix bug

		if (previewMode)
			previewTime += elapsed * 1000;

		if (endTime != -999999 && songPos - endTime > durationTime)
			KeyboardViewer.instance.removeObj(this);
	}
}

class DisBitmap extends Bitmap
{
	static public var Width:Int = KeyButton.size;
	static public var Height:Int = Std.int(KeyButton.size * 3);

	static public var colorArray:Array<FlxColor> = [];

	static public function addCache() {
		var BitmapData:BitmapData = new BitmapData(Width, Height, true, 0);
		var shape:Shape = new Shape();

		for (i in 0...Std.int(Height / 10))
		{
			shape.graphics.beginFill(FlxColor.WHITE, i / Std.int(Height / 10));
			shape.graphics.drawRect(0, i, Width, 1);
			shape.graphics.endFill();
		}
		shape.graphics.beginFill(FlxColor.WHITE);
		shape.graphics.drawRect(0, Std.int(Height / 10), Width, Height - Std.int(Height / 10));
		shape.graphics.endFill();
		BitmapData.draw(shape);

		var spr:FlxSprite = new FlxSprite();
		var newGraphic:FlxGraphic = FlxGraphic.fromBitmapData(BitmapData);
		spr.loadGraphic(newGraphic);

		Cache.setFrame('keyboardViewer', {graphic:null, frame:spr.frames});
	}
}