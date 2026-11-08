package states.serverConnectState;

import flixel.FlxG;
import flixel.FlxState;
import flixel.text.FlxText;
import flixel.ui.FlxButton;
import flixel.addons.ui.FlxInputText;
import flixel.util.FlxColor;
import server.nakama.NakamaStorage;
import server.nakama.NakamaClient.NakamaConfig;

class ServerConnectState extends FlxState {
    
    private var nakama:NakamaStorage;
    private var statusText:FlxText;
    private var usernameInput:FlxInputText;
    private var passwordInput:FlxInputText;
    private var loginButton:FlxButton;
    private var registerButton:FlxButton;
    
    override public function create():Void {
        super.create();
        
        // 配置
        var config:NakamaConfig = {
            host: "127.0.0.1",
            port: 7350,
            serverKey: "defaultkey",
            ssl: false
        };
        
        nakama = new NakamaStorage(config);
        
        // UI
        statusText = new FlxText(0, 50, 0, "Please login or register", 16);
        statusText.screenCenter(X);
        add(statusText);
        
        usernameInput = new FlxInputText(0, 100, 250, "Username");
        usernameInput.screenCenter(X);
        add(usernameInput);
        
        passwordInput = new FlxInputText(0, 150, 250, "Password");
        passwordInput.screenCenter(X);
        passwordInput.passwordMode = true;
        add(passwordInput);
        
        loginButton = new FlxButton(0, 220, "Login", onLoginClick);
        loginButton.screenCenter(X);
        add(loginButton);
        
        registerButton = new FlxButton(0, 260, "Register", onRegisterClick);
        registerButton.screenCenter(X);
        add(registerButton);
    }
    
    private function onLoginClick():Void {
        var username = StringTools.trim(usernameInput.text);
        var password = passwordInput.text;
        
        if (username == "" || password == "") {
            statusText.text = "Username and password cannot be empty";
            return;
        }
        
        statusText.text = "Logging in...";
        loginButton.active = false;
        
        nakama.secureLogin(username, password, function(response) {
            loginButton.active = true;
            
            if (response.success) {
                statusText.text = "Welcome, " + response.session.username + "!";
                trace("Login success! UserID: " + response.session.userId);
                // FlxG.switchState(new MainGameState());
            } else {
                statusText.text = "Login failed: " + response.error;
                passwordInput.text = "";
            }
        });
    }
    
    private function onRegisterClick():Void {
        var username = StringTools.trim(usernameInput.text);
        var password = passwordInput.text;
        
        if (username == "" || password == "") {
            statusText.text = "Username and password cannot be empty";
            return;
        }
        
        statusText.text = "Registering...";
        registerButton.active = false;
        
        nakama.secureRegister(username, password, function(response) {
            registerButton.active = true;
            
            if (response.success) {
                statusText.text = "Registration successful! You can now login.";
                trace("Register success! UserID: " + response.userId);
            } else {
                statusText.text = "Registration failed: " + response.error;
            }
        });
    }
}
