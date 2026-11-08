package options.groupData;

import openfl.Lib;

class GraphicsGroup extends OptionCata
{
	public function new(X:Float, Y:Float, width:Float, height:Float)
	{
		super(X, Y, width, height);

		var option:Option = new Option(this, 'Graphics', TITLE);
		addOption(option);
		
		/////--FPScounter--\\\\\

		var option:Option = new Option(this, 'FPScounter', TEXT);
		addOption(option);

		var option:Option = new Option(this, 'showFPS', BOOL);
		option.onChange = () -> changeWatermark();
		addOption(option);

		var option:Option = new Option(this, 'rainbowFPS', BOOL);
		addOption(option);

		var option:Option = new Option(this, 'fpsScale', FLOAT, [0, 5, 1]);
		option.onChange = () -> changeWatermark();
		addOption(option);

		var option:Option = new Option(this, 'fpsDisplayMode', STRING, ['TPS', 'FPS']);
		option.onChange = () -> changeWatermark();
		addOption(option);
		
		/////--Watermark--\\\\\

		var option:Option = new Option(this, 'Watermark', TEXT);
		addOption(option);

		var option:Option = new Option(this, 'showWatermark', BOOL);
		option.onChange = () -> changeWatermark();
		addOption(option);

		var option:Option = new Option(this, 'watermarkScale', FLOAT, [0, 5, 1]);
		option.onChange = () -> changeWatermark();
		addOption(option);

		changeHeight(0); //初始化真正的height
	}

	function changeWatermark() {
		Main.fpsVar.visible = ClientPrefs.data.showFPS;
		Main.fpsVar.scaleX = Main.fpsVar.scaleY = ClientPrefs.data.fpsScale;
		//Main.fpsVar.change();
		if (Main.watermark != null)
		{
			Main.watermark.scaleX = Main.watermark.scaleY = ClientPrefs.data.watermarkScale;
			Main.watermark.y = Lib.current.stage.stageHeight - 5 - Main.watermark.scaleY * Main.watermark.bitmapData.height;
			Main.watermark.visible = ClientPrefs.data.showWatermark;
		}
	}
}
