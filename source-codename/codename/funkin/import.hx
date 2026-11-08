#if !macro
import codename.funkin.backend.system.Main;
import codename.funkin.backend.assets.Paths;
import codename.funkin.backend.MusicBeatState;
import codename.funkin.backend.MusicBeatSubstate;
import codename.funkin.backend.MusicBeatGroup;
import codename.funkin.backend.FunkinSprite;
import codename.funkin.backend.utils.*;
import codename.funkin.backend.utils.TranslationUtil as TU;
import codename.funkin.backend.system.Logs;
import codename.funkin.options.Options;
import codename.funkin.game.PlayState;
import codename.funkin.backend.scripting.EventManager;

import openfl.utils.Assets;

import flixel.FlxSprite;
import flixel.FlxG;
import flixel.FlxBasic;
import flixel.FlxCamera;
import flixel.FlxObject;
import flixel.math.FlxMath;
import flixel.tweens.FlxEase;
import flixel.util.FlxDestroyUtil;

import codename.funkin.backend.system.Flags;
import codename.funkin.Types;

import codename.funkin.menus.ui.Alphabet;
import flixel.group.FlxGroup.FlxTypedGroup;
import flixel.group.FlxSpriteGroup.FlxTypedSpriteGroup;

using StringTools;
using codename.funkin.backend.utils.CoolUtil;
#end