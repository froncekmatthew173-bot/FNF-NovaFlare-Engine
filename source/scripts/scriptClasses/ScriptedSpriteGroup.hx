package scripts.scriptClasses;

/**
 * 相比原FlxSpriteGroup，这个更适用于在script class上继承（除了两个sm函数）
 */
@:noOverride("multiTransformChildren", "transformChildren")
class ScriptedSpriteGroup extends FlxSpriteGroup implements IScriptedClass {}