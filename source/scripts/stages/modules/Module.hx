package scripts.stages.modules;

/**
 * 另类的全局脚本执行替身
 * Alternative global script execution stunt double
 */
class Module {
	/**
	 * 替身的积极性，它将会决定是否被调用
	 * The enthusiasm of the substitute will determine whether it is called upon
	 */
	public var active:Bool;

	public function new() {
		this.active = true;
	}

	/**
	 * 当替身被启用的时候（事实是，只要注意点你完全可以使用new代替）
	 * When the substitute is activated
	 */
	public function onCreate() {}

	/**
	 * 当前state转到另一个state前的一瞬间
	 * The moment before the current state transitions to another state
	 */
	public function onStateSwitch() {}

	/**
	 * 前一刻state转到现在state的一瞬间
	 * The moment when the previous state transitions to the current state
	 */
	public function onStateSwitchPost() {}

	/**
	 * 在当前state调用create函数前的一瞬间
	 * At the moment before calling the create function in the current state
	 * @param state 当前的state - current state
	 */
	public function onStateCreate(state:FlxState) {}

	/**
	 * 当前游戏窗口被调整时的一瞬间
	 * The moment when the current game window is resized
	 * @param width 调整后的宽度 - Adjusted width
	 * @param height 调整后的高度 - Adjusted height
	 */
	public function onGameResized(width:Int, height:Int) {}

	/**
	 * 在游戏被重置（重启）前的一瞬间
	 * At the moment before the game is reset (restarted)
	 */
	public function onGameReset() {}

	/**
	 * 在游戏被重置（重启）后的一瞬间
	 * At the moment when the game is reset (restarted)
	 */
	public function onGameResetPost() {}

	/**
	 * 在游戏被启动前的一瞬间（无效）
	 * At the moment before the game is launched (invalid)
	 */
	public function onGameStart() {}

	/**
	 * 在游戏被启动后的一瞬间
	 * At the moment when the game is launched (invalid)
	 */
	public function onGameStartPost() {}

	/**
	 * 在游戏`（FlxG.game）`进行更新屏幕前的一瞬间
	 * The moment before updating the screen in the game `(FlxG.game)`
	 */
	public function onUpdate(elapsed:Float) {}

	/**
	 * 在游戏`（FlxG.game）`更新屏幕后的一瞬间
	 * The moment after updating the screen in the game `(FlxG.game)`
	 */
	public function onUpdatePost(elapsed:Float) {}

	/**
	 * 在游戏绘制画面前的一瞬间
	 * The moment before drawing the game screen
	 */
	public function onDraw() {}

	/**
	 * 在游戏绘制画面后的一瞬间
	 * At the moment after drawing the game graphics
	 */
	public function onDrawPost() {}

	/**
	 * 当游戏切至后台
	 * When the game switches to the background
	 */
	public function onFocusLost() {}

	/**
	 * 当从后台切回游戏里
	 * When switching back to the game from the background
	 */
	public function onFocusGained() {}
}