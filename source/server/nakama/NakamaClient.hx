package server.nakama;

import haxe.Http;
import haxe.Json;
import haxe.crypto.Base64;
import haxe.io.Bytes;

typedef NakamaConfig = {
    host: String,
    port: Int,
    serverKey: String,
    ssl: Bool
}

typedef Session = {
    token: String,
    refreshToken: String,
    userId: String,
    username: String,
    created: Bool,
    expireTime: Int
}

typedef Account = {
    user: {
        id: String,
        username: String,
        displayName: String,
        avatarUrl: String,
        langTag: String,
        location: String,
        timezone: String,
        metadata: Dynamic,
        createTime: String,
        updateTime: String
    },
    wallet: Dynamic,
    email: String,
    devices: Array<Dynamic>,
    customId: String,
    verifyTime: String,
    disableTime: String
}

class NakamaClient {
    private var config:NakamaConfig;
    private var baseUrl:String;
    private var session:Session;
    
    public function new(config:NakamaConfig) {
        this.config = config;
        var protocol = config.ssl ? "https" : "http";
        this.baseUrl = '$protocol://${config.host}:${config.port}';
    }
    
    // 获取 Basic Auth 头
    private function getBasicAuth():String {
        var credentials = config.serverKey + ":";
        var encoded = Base64.encode(Bytes.ofString(credentials));
        return "Basic " + encoded;
    }
    
    // 获取 Bearer Token 头（登录后使用）
    private function getBearerAuth():String {
        return "Bearer " + session.token;
    }
    
    // 处理 HTTP 请求
    private function request(method:String, path:String, ?data:Dynamic, ?useAuth:Bool = false, ?callback:Dynamic->Void):Void {
        var url = baseUrl + path;
        var http = new Http(url);
        
        // 设置请求头
        http.setHeader("Content-Type", "application/json");
        
        if (useAuth && session != null) {
            http.setHeader("Authorization", getBearerAuth());
        } else {
            http.setHeader("Authorization", getBasicAuth());
        }
        
        // 设置回调
        http.onData = function(responseData:String) {
            try {
                var json = Json.parse(responseData);
                callback(json);
            } catch (e:Dynamic) {
                callback({error: "Invalid JSON response", details: e});
            }
        };
        
        http.onError = function(error:String) {
            callback({error: error});
        };

        if (data != null && (method == "POST" || method == "PUT")) {
            http.setPostData(Json.stringify(data));
        }
        
        var isPost:Bool = (method == "POST" || method == "PUT");
        http.request(isPost);
    }
}