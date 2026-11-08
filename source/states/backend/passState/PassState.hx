package states.backend.passState;

import server.online.LoginClient;

import flixel.FlxGame;
import flixel.ui.FlxButton;
import flixel.addons.ui.FlxInputText;
import flixel.util.FlxSave;

import states.backend.initState.InitState;

class PassState extends FlxState {
    private var username:FlxInputText;
    private var password:FlxInputText;
    private var submitButton:FlxButton;
    
    private var loginclient:LoginClient;
    
    var save:FlxSave = new FlxSave();
    var usn:String = 'UserName';
    var psw:String = 'Password';
    
    override public function create():Void {
        super.create();
        FlxG.camera.bgColor = FlxColor.GRAY;
        
        loginclient = new LoginClient();
        
        save.bind("MyUserPass");
        
        if(save.data.user != null && save.data.pass != null){
            usn = save.data.user;
            psw = save.data.pass;
            loginFunc(save.data.user, save.data.pass);
        }

        username = new FlxInputText(
            FlxG.width / 2 - 150,
            FlxG.height / 2 - 60,
            300,
            usn,
            16,
            FlxColor.BLACK,
            FlxColor.WHITE
        );
        username.alignment = CENTER;
        add(username);
        
        password = new FlxInputText(
            FlxG.width / 2 - 150,
            FlxG.height / 2,
            300,
            psw,
            16,
            FlxColor.BLACK,
            FlxColor.WHITE
        );
        password.alignment = CENTER;
        add(password);
        
        submitButton = new FlxButton(
            FlxG.width / 2 - 50,
            FlxG.height / 2 + 60,
            "OK",
            onSubmit
        );
        submitButton.setGraphicSize(100, 40);
        submitButton.updateHitbox();
        add(submitButton);
    }
    
    private function onSubmit():Void {
        var user = username.text;
        var pass = password.text;
        save.data.user = user;
        save.data.pass = pass;
        save.flush();
        loginFunc(user, pass);
    }
    
    private function loginFunc(user:String, pass:String):Void{
        loginclient.decision = function(response:Dynamic) {
            if (response.message == "Good") {
                if(response.member == 'Admin'){
                    trace("登录成功: 欢迎登录");
                    FlxG.switchState(new InitState());
                }
            } else if(response.message == "Good" && response.member != 'Admin'){
                trace("登录成功: 但是你不是管理员");
            } else {
                trace("登录失败: " + response.message);
            }
        };
        
        loginclient.login(user,pass);
    }
}
