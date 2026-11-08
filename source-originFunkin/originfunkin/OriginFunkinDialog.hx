package originfunkin;

import mobile.backend.SUtil;

#if android
import android.Tools;
#end

/**
 * Native notices used by the Origin frontend.
 *
 * Android keeps the engine's native AlertDialog implementation. Desktop uses
 * NovaFlare's standard SUtil popup so Origin notices match the rest of NF.
 */
class OriginFunkinDialog
{
	static inline final ORIGIN_TITLE:String = "NovaFlare Engine x Friday Night Funkin' 0.8.7";
	static final ORIGIN_NOTICE:String =
		"You are playing the original FNF through NovaFlare Engine.\n\n"
		+ "This build is based on FNF 0.8.7, but it differs from the official game. "
		+ "If something breaks here, please do not report it to the Funkin' Crew.";

	public static function showOriginNotice(onContinue:Void->Void):Void
	{
		#if android
		Tools.showAlertDialog(ORIGIN_TITLE, ORIGIN_NOTICE, {
			name: "CONTINUE",
			func: onContinue
		});
		#else
		SUtil.showPopUp(ORIGIN_NOTICE, ORIGIN_TITLE);
		onContinue();
		#end
	}

}
