// NakamaAuth.hx
package server.nakama;

import server.util.EncryptUtil;

class NakamaAuth extends NakamaClient {
    
    public function new(config:NakamaClient.NakamaConfig) {
        super(config);
    }
    
    /**
     * 安全登录 - 整个请求 AES 加密
     */
    public function secureLogin(username:String, password:String, callback:Dynamic->Void):Void {
        trace('=== 安全登录开始 ===');
        
        var loginData = {
            username: username,
            password: password
        };
        
        var encryptedRequest = EncryptUtil.encryptRequest(loginData);
        trace('请求已加密: ' + encryptedRequest);
        
        // 3. 调用 RPC - 直接发送加密的字符串，不要包在对象里
        var path = '/v2/rpc/secure_login?http_key=defaulthttpkey&unwrap';
        
        var http = new haxe.Http(baseUrl + path);
        http.setHeader("Content-Type", "application/json");
        
        http.onData = function(responseData:String) {
            trace('收到响应: ' + responseData);
            try {
                // 服务端返回的应该是可以直接 JSON.parse 的字符串
                var response = haxe.Json.parse(responseData);
                
                if (response.success) {
                    // 保存会话
                    this.session = {
                        token: response.token,
                        refreshToken: response.refreshToken,
                        userId: response.userId,
                        username: response.username,
                        created: false,
                        expireTime: Math.floor(Date.now().getTime() / 1000) + 3600
                    };
                    
                    callback({ success: true, session: this.session });
                } else {
                    callback({ success: false, error: response.error });
                }
            } catch (e:Dynamic) {
                trace('解析响应失败: ' + e);
                callback({ success: false, error: "响应解析失败" });
            }
        };
        
        http.onError = function(error:String) {
            trace('网络错误: ' + error);
            callback({ success: false, error: "网络错误: " + error });
        };
        
        // 关键：直接发送加密的字符串，不要包在对象里
        trace('发送数据: ' + encryptedRequest);
        http.setPostData(encryptedRequest);  // 直接发送字符串
        http.request(true);
    }
    
    /**
     * 注册新用户
     */
    public function secureRegister(username:String, password:String, callback:Dynamic->Void):Void {
        var registerData = {
            username: username,
            password: password  // 明文密码
        };
        
        var encryptedRequest = EncryptUtil.encryptRequest(registerData);
        
        var path = '/v2/rpc/register?http_key=defaulthttpkey&unwrap';
        
        var http = new haxe.Http(baseUrl + path);
        http.setHeader("Content-Type", "application/json");
        
        http.onData = function(responseData:String) {
            try {
                var response = EncryptUtil.decryptResponse(responseData);
                callback(response);
            } catch (e:Dynamic) {
                callback({ success: false, error: "响应解密失败" });
            }
        };
        
        http.onError = function(error:String) {
            callback({ success: false, error: "网络错误: " + error });
        };
        
        http.setPostData(haxe.Json.stringify({ data: encryptedRequest }));
        http.request(true);
    }
}