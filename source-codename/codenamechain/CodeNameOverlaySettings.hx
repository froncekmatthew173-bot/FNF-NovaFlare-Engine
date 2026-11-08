package codenamechain;

import Main as NovaFlareMain;
import general.backend.ClientPrefs;
import general.objects.screen.MouseEffect;
import openfl.Lib;

/** Keeps the CNE Extra Setting and NovaFlare-owned overlay in sync. */
class CodeNameOverlaySettings
{
	public static function applyWatermarkScale(value:Float, persist:Bool = true):Void
	{
		value = Math.max(0, Math.min(5, value));
		ClientPrefs.data.watermarkScale = value;

		if (NovaFlareMain.watermark != null) {
			NovaFlareMain.watermark.scaleX = NovaFlareMain.watermark.scaleY = value;
			NovaFlareMain.watermark.y = Lib.current.stage.stageHeight - 5
				- NovaFlareMain.watermark.scaleY * NovaFlareMain.watermark.bitmapData.height;
		}

		if (persist) ClientPrefs.saveSettings();
	}

	public static function applyMouseEffects(value:Bool):Void
	{
		MouseEffect.setUserEffectsEnabled(value);
	}
}
